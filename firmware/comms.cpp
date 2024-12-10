#include <string.h>

#include "system.h"
#include "comms.h"
#include "pico_link.h"

#include "comms.pio.h"

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"

static PIO comms_pio = pio1;

FIFO<64> comms_out_fifo;
FIFO<64> comms_in_fifo;
uint32_t comms_out_deferred_req;
uint32_t comms_out_deferred_ack;
uint32_t comms_in_empty_req;
uint32_t comms_in_empty_ack;

struct CommsRegisters
{
    uint8_t magic[4];
    
    // only the least significant byte is relevant for these, using 32-bits to avoid potential atomic issues
    uint32_t active;
    uint32_t pending;
    uint32_t in_seq;
    uint32_t out_seq;
    uint32_t cycle_count;
    uint32_t debug1;
    uint32_t debug2;

    uint8_t reserved0[256 - (8 * 4)];

    uint32_t cycle_reset;
    uint8_t reserved1[256 - (1 * 4)];

    uint32_t in_byte;
    uint8_t reserved2[256 - (1 * 4)];
    
    uint8_t out_area[256];
};

static_assert(sizeof(CommsRegisters) == 1024);

static CommsRegisters *comms_reg = (CommsRegisters *)0x0;
static uint32_t comms_reg_addr = 0;

void comms_irq_handler()
{
    uint32_t addr = pio_sm_get(pio1, 0);
    comms_reg->debug2 = addr;
    if (addr & 0x100)
    {
        if (comms_reg)
        {
            comms_out_fifo.push(addr & 0xff);
            if (comms_out_fifo.is_full())
            {
                comms_out_deferred_req++;
            }
            else
            {
                comms_reg->out_seq++;
            }
        }
    }
    else if (addr == 0x000)
    {
        comms_in_fifo.pop();

        if (comms_reg)
        {
            comms_reg->debug1++;
            if (comms_in_fifo.is_empty())
            {
                comms_in_empty_req++;
                comms_reg->pending = 0;
            }
            else
            {
                comms_reg->in_byte = comms_in_fifo.peek();
                comms_reg->in_seq++;
            }
        }
    }
}

static void pio_set_y(PIO p, uint sm, uint32_t v)
{
    uint32_t shift_save = p->sm[sm].shiftctrl;
    p->sm[sm].shiftctrl &= ~PIO_SM0_SHIFTCTRL_IN_SHIFTDIR_BITS;

    const uint instr_shift = pio_encode_in(pio_y, 4);
    const uint instr_mov = pio_encode_mov(pio_y, pio_isr);
    for (int i = 7; i >= 0; i--)
    {
        const uint32_t nibble = (v >> (i * 4)) & 0xf;
        pio_sm_exec(p, sm, pio_encode_set(pio_y, nibble));
        pio_sm_exec(p, sm, instr_shift);
    }
    pio_sm_exec(p, sm, instr_mov);
    p->sm[sm].shiftctrl &= shift_save;
}

static void comms_start_programs(uint32_t addr, CommsRegisters *regs)
{
    pio_clear_instruction_memory(comms_pio);

    uint32_t offset_access = pio_add_program(comms_pio, &detect_access_program);
    uint32_t offset_clock = pio_add_program(comms_pio, &detect_clock_program);

    pio_sm_config c_access = detect_access_program_get_default_config(offset_access);
    sm_config_set_in_pins(&c_access, 0);
    pio_sm_init(comms_pio, 0, offset_access, &c_access);
    pio_set_y(comms_pio, 0, (addr + 0x200) >> 9);
    pio_sm_set_enabled(comms_pio, 0, true);
    pio_set_irq0_source_enabled(comms_pio, pis_sm0_rx_fifo_not_empty, true);

    pio_gpio_init(comms_pio, CLOCK_PIN);
    gpio_set_dir(CLOCK_PIN, false);
    gpio_set_input_enabled(CLOCK_PIN, true);

    pio_sm_config c_clock = detect_clock_program_get_default_config(offset_clock);
    sm_config_set_in_pins(&c_clock, 0);
    sm_config_set_in_shift(&c_clock, true, false, 32);
    pio_sm_init(comms_pio, 3, offset_clock, &c_clock);
    pio_set_y(comms_pio, 3, addr + offsetof(CommsRegisters, cycle_reset));
    pio_sm_set_enabled(comms_pio, 3, true);

    dma_channel_config c = dma_channel_get_default_config(2);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, DREQ_PIO1_RX3);
    channel_config_set_high_priority(&c, true);
    channel_config_set_irq_quiet(&c, true);
    channel_config_set_chain_to(&c, 3);
    dma_channel_configure(2, &c, &regs->cycle_count, &pio1_hw->rxf[3], 0xffffffff, false);
    channel_config_set_chain_to(&c, 2);
    dma_channel_configure(3, &c, &regs->cycle_count, &pio1_hw->rxf[3], 0xffffffff, true);
}

static void comms_end_programs()
{
    pio_sm_set_enabled(comms_pio, 0, false);
    pio_sm_set_enabled(comms_pio, 1, false);
    pio_sm_set_enabled(comms_pio, 2, false);
    pio_sm_set_enabled(comms_pio, 3, false);
}

static void update_comms_out(uint8_t *outbytes, int *outcount, int max_outcount)
{
    while(comms_out_deferred_ack != comms_out_deferred_req)
    {
        comms_reg->out_seq++;
        comms_out_deferred_ack++;
    }

    while( comms_out_fifo.count() > 0 )
    {
        outbytes[*outcount] = comms_out_fifo.pop();
        (*outcount)++;

        if (*outcount == max_outcount)
        {
            pl_send_payload(PacketType::CommsData, outbytes, *outcount);
            *outcount = 0;
        }
    }
}

void comms_init()
{
    irq_set_exclusive_handler(PIO1_IRQ_0, comms_irq_handler);
}

void comms_begin_session(uint32_t addr, uint8_t *rom_base)
{
    uint32_t ints = save_and_disable_interrupts();
    comms_out_fifo.clear();
    comms_in_fifo.clear();
    comms_out_deferred_ack = 0;
    comms_out_deferred_req = 0;
    comms_in_empty_ack = 0;
    comms_in_empty_req = 1;
    comms_reg_addr = (addr & ADDR_MASK & ~0x3ff);
    comms_reg = (CommsRegisters *)(rom_base + comms_reg_addr);
    comms_reg->active = 0;
    comms_reg->pending = 0;
    comms_reg->in_seq = 0;
    comms_reg->out_seq = 0;
    memcpy(comms_reg->magic, "PICO", 4);
    
    comms_start_programs(comms_reg_addr, comms_reg);

    restore_interrupts(ints);

    irq_set_enabled(PIO1_IRQ_0, true);

    comms_reg->active = 1;
}

void comms_end_session()
{
    if (comms_reg == nullptr) return;

    irq_set_enabled(PIO1_IRQ_0, false);

    comms_reg->active = 0;
    comms_reg = nullptr;

    comms_end_programs();
}

bool comms_update(const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    if (comms_reg == nullptr) return true;

    absolute_time_t end_time = make_timeout_time_ms(timeout_ms);

    uint8_t outbytes[MAX_PKT_PAYLOAD];
    int outcount = 0;

    update_comms_out(outbytes, &outcount, sizeof(outbytes));
    
    uint incount = 0;
    while (incount < len)
    {
        comms_reg->pending = 1;
        do
        {
            update_comms_out(outbytes, &outcount, sizeof(outbytes));
            if (absolute_time_diff_us(get_absolute_time(), end_time) < 0)
            {
                return false;
            }
        } while (comms_in_fifo.is_full());

        comms_in_fifo.push(data[incount]);
        incount++;

        if(comms_in_empty_ack != comms_in_empty_req)
        {
            comms_reg->in_byte = comms_in_fifo.peek();
            comms_reg->in_seq++;
            comms_in_empty_ack++;
        }
    }
    
    if (outcount > 0)
    {
        pl_send_payload(PacketType::CommsData, outbytes, outcount);
    }

    return true;
}


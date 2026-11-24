#include <hardware/dma.h>
#include <hardware/irq.h>
#include <string.h>

#include "comms.h"
#include "pico_link.h"
#include "pio_programs.h"
#include "system.h"

#include "hardware/pio.h"
#include "pico/time.h"

FIFO<32> comms_out_fifo;
FIFO<32> comms_in_fifo;
uint8_t comms_out_deferred_req;
uint8_t comms_out_deferred_ack;
uint8_t comms_in_empty_req;
uint8_t comms_in_empty_ack;

struct CommsRegisters
{
    uint8_t magic[4];

    // only the least significant byte is relevant for these, using 32-bits to avoid potential atomic issues
    uint32_t active;
    uint32_t pending;
    uint32_t in_seq;
    uint32_t out_seq;
    uint32_t tick_count;
    uint32_t debug1;
    uint32_t debug2;

    uint8_t reserved0[256 - (8 * 4)];

    uint32_t tick_reset;
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
    uint32_t addr = pio_sm_get(prg_comms_detect.pio(), prg_comms_detect.sm);
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

// The comms_detect programs (comms.pio) read the address from GPIO 0, the window as address bits
// 9-17, and the enables from BASE_OE_PIN. The two-pin versions name their enable pins.
static_assert(BASE_ADDR_PIN == 0 && N_ADDR_PINS <= 18);
static_assert(BASE_DATA_PIN >= N_ADDR_PINS); // window bits past the address read as 0
#if defined(BOARD_32P_TCA)
static_assert(BASE_OE_PIN == 20 && N_OE_PINS == 2);
#elif defined(BOARD_28P)
static_assert(BASE_OE_PIN == 24 && N_OE_PINS == 2);
#else
static_assert(N_OE_PINS == 1);
#endif

static void comms_start_programs(uint32_t addr, CommsRegisters *regs)
{
    if (prg_comms_detect.valid())
    {
        PRG_LOCAL(prg_comms_detect, p, sm, offset, cfg);
        sm_config_set_in_pins(&cfg, 0);
        // the access strobe (the first of the two on TCA and 28P); BUF_OE_PIN can't be used,
        // rom.cpp forces its input low
        sm_config_set_jmp_pin(&cfg, BASE_OE_PIN);
        pio_sm_init(p, sm, offset, &cfg);
        pio_set_y(p, sm, (addr + 0x200) >> 9);
        pio_sm_set_enabled(p, sm, true);
        pio_set_irq0_source_enabled(p, (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + sm), true);
        irq_set_exclusive_handler(PIO_IRQ_NUM(p, 0), comms_irq_handler);
        irq_set_enabled(PIO_IRQ_NUM(p, 0), true);
    }

#if defined(FEATURE_CLOCK)
    if (prg_comms_clock.valid())
    {
        pio_gpio_init(prg_comms_clock.pio(), CLOCK_PIN);
        gpio_set_dir(CLOCK_PIN, false);
        gpio_set_input_enabled(CLOCK_PIN, true);

        PRG_LOCAL(prg_comms_clock, p, sm, offset, cfg);
        sm_config_set_in_pins(&cfg, 0);
        sm_config_set_in_shift(&cfg, true, false, 32);
        pio_sm_init(p, sm, offset, &cfg);
        pio_set_y(p, sm, addr + offsetof(CommsRegisters, tick_reset));
        pio_sm_set_enabled(p, sm, true);

        dma_channel_config c = dma_channel_get_default_config(DMA_CH_CLOCK_PING);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, PIO_DREQ_NUM(prg_comms_clock.pio(), prg_comms_clock.sm, false));
        channel_config_set_high_priority(&c, true);
        channel_config_set_irq_quiet(&c, true);
        channel_config_set_chain_to(&c, DMA_CH_CLOCK_PONG);
        dma_channel_configure(DMA_CH_CLOCK_PING, &c, &regs->tick_count, &prg_comms_clock.pio()->rxf[prg_comms_clock.sm],
                              0xffffffff, false);
        channel_config_set_chain_to(&c, DMA_CH_CLOCK_PING);
        dma_channel_configure(DMA_CH_CLOCK_PONG, &c, &regs->tick_count, &prg_comms_clock.pio()->rxf[prg_comms_clock.sm],
                              0xffffffff, true);
    }
#endif // FEATURE_CLOCK
}

static void comms_end_programs()
{
    comms_reg->debug1 = 0xff00;
    if (prg_comms_detect.valid())
    {
        PRG_LOCAL(prg_comms_detect, pio, sm, offset, cfg);

        comms_reg->debug1 = 0xff01;

        pio_sm_set_enabled(pio, sm, false);
        comms_reg->debug1 = 0xff02;
        pio_sm_clear_fifos(pio, sm);
        comms_reg->debug1 = 0xff03;
        pio_set_irq0_source_enabled(pio, (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + sm), false);
        comms_reg->debug1 = 0xff04;
        irq_set_enabled(PIO_IRQ_NUM(pio, 0), false);
        comms_reg->debug1 = 0xff05;
    }

#if defined(FEATURE_CLOCK)
    if (prg_comms_clock.valid())
    {
        PRG_LOCAL(prg_comms_clock, pio, sm, offset, cfg);

        pio_sm_set_enabled(pio, sm, false);
        pio_sm_clear_fifos(pio, sm);
        dma_channel_abort(DMA_CH_CLOCK_PING);
        dma_channel_abort(DMA_CH_CLOCK_PONG);
    }
#endif // FEATURE_CLOCK
}

// Sends one packet of what has arrived, once the last has gone (pl_tx_idle), so it never blocks:
// usb_send only returns once tud_task() has taken the earlier packets, and this may be called from
// inside it. Meanwhile the bytes collect in comms_out_fifo, and while that is full the program
// waits on out_seq.
static void update_comms_out()
{
    if (comms_out_fifo.count() > 0 && pl_tx_idle())
    {
        const uint32_t count = MIN(comms_out_fifo.count(), MAX_PKT_PAYLOAD);
        uint8_t outbytes[MAX_PKT_PAYLOAD];
        for (uint32_t i = 0; i < count; i++)
        {
            outbytes[i] = comms_out_fifo.pop();
        }
        pl_send_payload(PacketType::CommsData, outbytes, count);
    }

    // acknowledge the bytes that arrived while the fifo was full, once there is room for more
    while (comms_out_deferred_ack != comms_out_deferred_req && !comms_out_fifo.is_full())
    {
        comms_reg->out_seq++;
        comms_out_deferred_ack++;
    }
}

void comms_begin_session(uint32_t addr, uint8_t *rom_base)
{
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

    comms_reg->active = 1;
}

void comms_end_session()
{
    if (comms_reg == nullptr) return;

    comms_end_programs();

    comms_reg->active = 0;
    comms_reg = nullptr;
}

bool comms_update(const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    if (comms_reg == nullptr) return true;

    absolute_time_t end_time = make_timeout_time_ms(timeout_ms);

    update_comms_out();

    uint incount = 0;
    while (incount < len)
    {
        comms_reg->pending = 1;
        do
        {
            update_comms_out();
            if (absolute_time_diff_us(get_absolute_time(), end_time) < 0)
            {
                return false;
            }
        } while (comms_in_fifo.is_full());

        comms_in_fifo.push(data[incount]);
        incount++;

        if (comms_in_empty_ack != comms_in_empty_req)
        {
            comms_reg->in_byte = comms_in_fifo.peek();
            comms_reg->in_seq++;
            comms_in_empty_ack++;
        }
    }

    return true;
}

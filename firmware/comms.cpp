#include <hardware/irq.h>
#include <string.h>

#include "system.h"
#include "comms.h"
#include "pico_link.h"
#include "pio_programs.h"

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
    uint32_t in_byte;
    uint32_t in_seq;
    uint32_t out_seq;

    uint8_t reserved[256 - (6 * 4)];

    uint8_t out_area[256];
};

static_assert(sizeof(CommsRegisters) == 512);

static CommsRegisters *comms_reg = (CommsRegisters *)0x0;
static uint32_t comms_reg_addr = 0;

void comms_out_irq_handler()
{
    uint8_t byte = pio_sm_get(prg_comms_write.pio(), prg_comms_read.sm) & 0xff; // this must be valid at this point
    if (comms_reg)
    {
        comms_out_fifo.push(byte);
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

void comms_in_irq_handler()
{
    pio_sm_get(prg_comms_read.pio(), prg_comms_read.sm); // don't care
    comms_in_fifo.pop();

    if (comms_reg)
    {
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

static void comms_start_programs(uint32_t addr, uint32_t byte_offset)
{
    if (prg_comms_write.valid())
    {
        PRG_LOCAL(prg_comms_write, pio, sm, offset, cfg);

        sm_config_set_in_pins(&cfg, 0);
        pio_sm_init(pio, sm, offset, &cfg);
        pio_sm_clear_fifos(pio, sm);
        pio_sm_set_enabled(pio, sm, true);
        pio_sm_put_blocking(pio, sm, (addr + 0x100) >> 8);
        pio_set_irq0_source_enabled(pio, (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + sm), true);
        irq_set_exclusive_handler(PIO_IRQ_NUM(pio, 0), comms_out_irq_handler);
        irq_set_enabled(PIO_IRQ_NUM(pio, 0), true);
    }

    if (prg_comms_read.valid())
    {
        PRG_LOCAL(prg_comms_read, pio, sm, offset, cfg);

        sm_config_set_in_pins(&cfg, 0);
        pio_sm_init(pio, sm, offset, &cfg);
        pio_sm_clear_fifos(pio, sm);
        pio_sm_set_enabled(pio, sm, true);
        pio_sm_put_blocking(pio, sm, addr + byte_offset);
        pio_set_irq1_source_enabled(pio, (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + sm), true);
        irq_set_exclusive_handler(PIO_IRQ_NUM(pio, 1), comms_in_irq_handler);
        irq_set_enabled(PIO_IRQ_NUM(pio, 1), true);
    } 
}

static void comms_end_programs()
{
    if (prg_comms_write.valid())
    {
        PRG_LOCAL(prg_comms_write, pio, sm, offset, cfg);

        pio_sm_set_enabled(pio, sm, false);
        pio_sm_clear_fifos(pio, sm);
        pio_set_irq0_source_enabled(pio, (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + sm), false);
        irq_set_exclusive_handler(PIO_IRQ_NUM(pio, 0), __unhandled_user_irq);
        irq_set_enabled(PIO_IRQ_NUM(pio, 0), false);
    }

    if (prg_comms_read.valid())
    {
        PRG_LOCAL(prg_comms_read, pio, sm, offset, cfg);

        pio_sm_set_enabled(pio, sm, false);
        pio_sm_clear_fifos(pio, sm);
        pio_set_irq1_source_enabled(pio, (pio_interrupt_source_t)(pis_sm0_rx_fifo_not_empty + sm), false);
        
        irq_set_exclusive_handler(PIO_IRQ_NUM(pio, 1), __unhandled_user_irq);
        irq_set_enabled(PIO_IRQ_NUM(pio, 1), false);
    }
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

void comms_begin_session(uint32_t addr, uint8_t *rom_base)
{
    comms_out_fifo.clear();
    comms_in_fifo.clear();
    comms_out_deferred_ack = 0;
    comms_out_deferred_req = 0;
    comms_in_empty_ack = 0;
    comms_in_empty_req = 1;
    comms_reg_addr = addr & ADDR_MASK & ~0x1ff;
    comms_reg = (CommsRegisters *)(rom_base + comms_reg_addr);
    memset(comms_reg, 0, sizeof(CommsRegisters));
    memcpy(comms_reg->magic, "PICO", 4);
    
    comms_start_programs(comms_reg_addr, offsetof(CommsRegisters, in_byte));

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


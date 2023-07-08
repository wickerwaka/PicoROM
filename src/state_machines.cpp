
#include "state_machines.h"
#include "system.h"

#include "data_bus.pio.h"

static PIO data_pio = pio0;
static PIO addr_pio = pio1;

void init_data_bus_programs()
{
    uint sm_data = pio_claim_unused_sm(data_pio, true);
    uint sm_oe = pio_claim_unused_sm(data_pio, true);

    // Assign data and oe pins to pio
    for( uint ofs = 0; ofs < N_DATA_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_DATA_PIN + ofs);
        gpio_set_input_enabled(BASE_DATA_PIN + ofs, false);
    }

    for( uint ofs = 0; ofs < N_OE_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_OE_PIN + ofs);
    }

    for( uint ofs = 0; ofs < N_BUF_OE_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_BUF_OE_PIN + ofs);
        gpio_set_input_enabled(BASE_BUF_OE_PIN + ofs, false);
    }

    // OUTPUT_BUFFER==0 does this in the oe program
#if OUTPUT_BUFFER==1
    pio_sm_set_consecutive_pindirs(data_pio, sm_data, BASE_DATA_PIN, N_DATA_PINS, true);
#endif

    // set out/in bases
    uint offset_data = pio_add_program(data_pio, &output_program);
    pio_sm_config c_data = output_program_get_default_config(offset_data);

    sm_config_set_out_pins(&c_data, BASE_DATA_PIN, N_DATA_PINS);
    sm_config_set_out_shift(&c_data, true, true, N_DATA_PINS);
    pio_sm_init(data_pio, sm_data, offset_data, &c_data);
    pio_sm_set_enabled(data_pio, sm_data, true);

    // set oe pin directions, data pin direction will be set by the sm
    pio_sm_set_consecutive_pindirs(data_pio, sm_oe, BASE_OE_PIN, N_OE_PINS, false);
    pio_sm_set_consecutive_pindirs(data_pio, sm_oe, BASE_BUF_OE_PIN, N_BUF_OE_PINS, true);

#if OUTPUT_BUFFER==1
    uint offset_oe = pio_add_program(data_pio, &output_enable_buffer_program);
    pio_sm_config c_oe = output_enable_buffer_program_get_default_config(offset_oe);
    sm_config_set_in_pins(&c_oe, BASE_OE_PIN);
    sm_config_set_set_pins(&c_oe, BASE_BUF_OE_PIN, N_BUF_OE_PINS);
#else
    uint offset_oe = pio_add_program(data_pio, &output_enable_program);
    pio_sm_config c_oe = output_enable_program_get_default_config(offset_oe);
    sm_config_set_in_pins(&c_oe, BASE_OE_PIN);
    sm_config_set_out_pins(&c_oe, BASE_DATA_PIN, N_DATA_PINS);
#endif
    pio_sm_init(data_pio, sm_oe, offset_oe, &c_oe);
    pio_sm_set_enabled(data_pio, sm_oe, true);
}

io_wo_32 *get_data_bus_fifo()
{
    return &data_pio->txf[0];
}


void start_comms_programs(uint32_t addr, uint32_t byte_offset)
{
    pio_clear_instruction_memory(addr_pio);

    uint32_t offset_write = pio_add_program(addr_pio, &detect_write_program);
    uint32_t offset_read = pio_add_program(addr_pio, &detect_read_program);

    pio_sm_config c_write = detect_write_program_get_default_config(offset_write);
    sm_config_set_in_pins(&c_write, 0);
    pio_sm_init(addr_pio, 0, offset_write, &c_write);
    pio_sm_set_enabled(addr_pio, 0, true);
    pio_sm_put_blocking(addr_pio, 0, (addr + 0x100) >> 8);
    pio_set_irq0_source_enabled(addr_pio, pis_sm0_rx_fifo_not_empty, true);

    pio_sm_config c_read = detect_read_program_get_default_config(offset_read);
    sm_config_set_in_pins(&c_read, 0);
    pio_sm_init(addr_pio, 1, offset_read, &c_read);
    pio_sm_set_enabled(addr_pio, 1, true);
    pio_sm_put_blocking(addr_pio, 1, addr + byte_offset);
    pio_set_irq1_source_enabled(addr_pio, pis_sm1_rx_fifo_not_empty, true);
}

void end_comms_programs()
{
    pio_sm_set_enabled(addr_pio, 0, false);
    pio_sm_set_enabled(addr_pio, 1, false);
}

bool comms_poll_write(uint8_t *out)
{
    if (pio_sm_get_rx_fifo_level(addr_pio, 0) > 0)
    {
        *out = pio_sm_get(addr_pio, 0);
        return true;
    }

    return false;
}


bool comms_poll_read()
{
    bool was_read = false;
    while( pio_sm_get_rx_fifo_level(addr_pio, 1) > 0 )
    {
        pio_sm_get(addr_pio, 1);
        was_read = true;
    }

    return was_read;
}



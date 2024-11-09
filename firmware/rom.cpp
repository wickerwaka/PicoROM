#include "hardware/structs/bus_ctrl.h"
#include "pico/multicore.h"

#include "rom.h"
#include "system.h"

#include "data_bus.pio.h"

static PIO data_pio = pio0;

uint8_t *rom_data = (uint8_t *)0x21000000; // Start of 4 64kb sram banks

uint32_t core1_stack[8];
static void __attribute__((noreturn, section(".time_critical.core1_rom_loop"))) rom_loop()
{
    // r0 - SIO BASE
    // r1 - Address and data
    // r4 - BUF_OE mask
    // r5 - OE mask
    // r6 - Data pin mask
    // r7 - ROM Base
    __asm__ volatile (
        "    ldr r0, =%c0        \n"
        "    ldr r7, =%c1        \n"
        "    ldr r4, =%c2        \n"
        "    ldr r5, =%c3        \n"
        "    ldr r6, =%c4        \n"
        "    ldr r1, =0          \n"
        "re_enable:              \n"
        "    str r6, [r0,#0x24]  \n" // Enable data pin output via GPIO_OE_SET - 1 cycle
        "enabled:                \n"
        "    ldrb r1, [r7, r1]   \n" // r1 = rom_data[r1] - 2 cycles
        "    lsl r1, r1, #22     \n" // Shift data over to data pin location - 1 cycle
        "    str r1, [r0,#0x10]  \n" // Write data via GPIO_OUT, BUF_OE is set low also - 1 cycle
        "    ldr r1, [r0,#0x04]  \n" // Read address, OE and CS via GPIO_IN - 1 cycle
        "    sub r1, r1, r5      \n" // Subtract OE/CS mask from address - 1 cycle
        "    bpl enabled         \n" // if that didn't cause a carry, repeat - 2 cycles
        "disabled:               \n"
        "    str r4, [r0,#0x14]  \n" // Set BUF_OE high via GPIO_OUT_SET - 1 cycle 
        "    str r6, [r0,#0x28]  \n" // Disable data pin output via GPIO_OE_CLR - 1 cycle
        "    ldr r1, [r0,#0x04]  \n" // // Read address, OE and CS via GPIO_IN - 1 cycle
        "    sub r1, r1, r5      \n" // Subtract OE/CS mask from address - 1 cycle
        "    bpl re_enable       \n" // if that didn't cause a carry, go back to enabled loop - 2 cycles
        "    b disabled          \n" // loop - 2 cycles
        :
        : "i" (SIO_BASE), "i" (SRAM0_BASE), "i" (BUFFER_PIN_MASK), "i" (OE_PIN_MASK), "i" (DATA_PIN_MASK)
        :
    );

    __builtin_unreachable();
}

static uint sm_report = 0;
static uint sm_tca = 0;
void rom_init_programs()
{
    uint sm_oe = pio_claim_unused_sm(data_pio, true);
    uint sm_data = pio_claim_unused_sm(data_pio, true);
    
    sm_report = pio_claim_unused_sm(data_pio, true);
    sm_tca = pio_claim_unused_sm(data_pio, true);

    // Assign data and oe pins to pio
    for( uint ofs = 0; ofs < N_DATA_PINS; ofs++ )
    {
        gpio_init(BASE_DATA_PIN + ofs);
        gpio_set_dir(BASE_DATA_PIN + ofs, true);
        gpio_set_drive_strength(BASE_DATA_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_input_enabled(BASE_DATA_PIN + ofs, false);
        gpio_set_drive_strength(BASE_DATA_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
    }

    for( uint ofs = 0; ofs < N_OE_PINS; ofs++ )
    {
        gpio_init(BASE_OE_PIN + ofs);
        gpio_set_inover(BASE_OE_PIN + ofs, GPIO_OVERRIDE_INVERT);
    }

    for( uint ofs = 0; ofs < N_BUF_OE_PINS; ofs++ )
    {
        gpio_init(BASE_BUF_OE_PIN + ofs);
        gpio_set_drive_strength(BASE_BUF_OE_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_input_enabled(BASE_BUF_OE_PIN + ofs, false);
        gpio_set_drive_strength(BASE_BUF_OE_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
    }

    pio_gpio_init(data_pio, TCA_EXPANDER_PIN);
    gpio_set_input_enabled(TCA_EXPANDER_PIN, false);
    gpio_set_drive_strength(TCA_EXPANDER_PIN, GPIO_DRIVE_STRENGTH_2MA);

    pio_sm_set_consecutive_pindirs(data_pio, sm_data, BASE_DATA_PIN, N_DATA_PINS, true);

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

    uint offset_oe = pio_add_program(data_pio, &output_enable_buffer_program);
    pio_sm_config c_oe = output_enable_buffer_program_get_default_config(offset_oe);
    sm_config_set_in_pins(&c_oe, BASE_OE_PIN);
    sm_config_set_out_pins(&c_oe, BASE_DATA_PIN, N_DATA_PINS);
    sm_config_set_set_pins(&c_oe, BASE_BUF_OE_PIN, N_BUF_OE_PINS);
    sm_config_set_out_pins(&c_oe, BASE_DATA_PIN, N_DATA_PINS);
    
    pio_sm_init(data_pio, sm_oe, offset_oe, &c_oe);
    pio_sm_set_enabled(data_pio, sm_oe, true);
   
    uint offset_report = pio_add_program(data_pio, &output_enable_report_program);
    pio_sm_config c_report = output_enable_report_program_get_default_config(offset_report);
    sm_config_set_in_pins(&c_report, BASE_OE_PIN);
    pio_set_irq0_source_enabled(data_pio, (enum pio_interrupt_source) ((uint) pis_interrupt0 + sm_report), false);
    pio_set_irq1_source_enabled(data_pio, (enum pio_interrupt_source) ((uint) pis_interrupt0 + sm_report), false);
    pio_interrupt_clear(data_pio, sm_report);

    pio_sm_init(data_pio, sm_report, offset_report, &c_report);
    pio_sm_set_enabled(data_pio, sm_report, true);

    pio_sm_set_consecutive_pindirs(data_pio, sm_tca, TCA_EXPANDER_PIN, 1, true);
    pio_sm_set_pins_with_mask(data_pio, sm_tca, 0xffffffff, 1 << TCA_EXPANDER_PIN);

    uint offset_tca = pio_add_program(data_pio, &tca5405_program);
    pio_sm_config c_tca = tca5405_program_get_default_config(offset_tca);
    sm_config_set_out_pins(&c_tca, TCA_EXPANDER_PIN, 1);
    sm_config_set_clkdiv(&c_tca, 1000);
    sm_config_set_out_shift(&c_tca, true, true, 10); // 4-bits of preample, 5-bits of data, 1-end bit 

    pio_sm_init(data_pio, sm_tca, offset_tca, &c_tca);
    pio_sm_set_enabled(data_pio, sm_tca, true);

    tca_set_pins(0x00);
    tca_set_pins(0x00);
}

uint8_t *rom_get_buffer()
{
    return rom_data;
}

void rom_service_start()
{
    // give core1 bus priority
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    multicore_reset_core1();
    multicore_launch_core1_with_stack(rom_loop, core1_stack, sizeof(core1_stack));
}

void rom_service_stop()
{
    multicore_reset_core1();
}

bool rom_check_oe()
{
    if( pio_interrupt_get(data_pio, sm_report) )
    {
        pio_interrupt_clear(data_pio, sm_report);
        return true;
    }
    return false;
}

static uint8_t tca_pins_state = 0x0;
void tca_set_pins(uint8_t pins)
{
    uint32_t bitstream = 0b1000001010 | ((pins & 0x1f) << 4);
    data_pio->txf[sm_tca] = bitstream;
    tca_pins_state = pins; 
}

void tca_set_pin(int pin, bool en)
{
    uint8_t new_state = tca_pins_state;
    if (en)
        new_state |= (1 << pin);
    else
        new_state &= ~(1 << pin);
    
    if (new_state != tca_pins_state)
    {
        tca_set_pins(new_state);
    }
}


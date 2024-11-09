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
    register uint32_t r0 __asm__("r0") = (uint32_t)rom_data;
    register uint32_t r1 __asm__("r1") = ADDR_MASK;
    register uint32_t r2 __asm__("r2") = (uint32_t)&data_pio->txf[0];

    __asm__ volatile (
        "ldr r5, =0xd0000004 \n\t"
        "loop: \n\t"
        "ldr r3, [r5] \n\t" // 1
        "and r3, r1 \n\t" // 1
        "ldrb r3, [r0, r3] \n\t" // 2
        "strb r3, [r2] \n\t" // 1
        "b loop \n\t" // 2
        : "+r" (r0), "+r" (r1), "+r" (r2)
        :
        : "r5", "cc", "memory"
    );

    __builtin_unreachable();
}

static uint sm_report = 0;
static uint sm_tca = 0;
void rom_init_programs()
{
    uint sm_data = pio_claim_unused_sm(data_pio, true);
    uint sm_oe = pio_claim_unused_sm(data_pio, true);
    
    sm_report = pio_claim_unused_sm(data_pio, true);
    sm_tca = pio_claim_unused_sm(data_pio, true);

    // Assign data and oe pins to pio
    for( uint ofs = 0; ofs < N_DATA_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_DATA_PIN + ofs);
        gpio_set_input_enabled(BASE_DATA_PIN + ofs, false);
        gpio_set_drive_strength(BASE_DATA_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
    }

    for( uint ofs = 0; ofs < N_OE_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_OE_PIN + ofs);
        gpio_set_pulls(BASE_OE_PIN + ofs, true, false);
    }

    for( uint ofs = 0; ofs < N_BUF_OE_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_BUF_OE_PIN + ofs);
        gpio_set_input_enabled(BASE_BUF_OE_PIN + ofs, false);
        gpio_set_drive_strength(BASE_BUF_OE_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
    }

#if TCA_EXPANDER
    pio_gpio_init(data_pio, TCA_EXPANDER_PIN);
    gpio_set_input_enabled(TCA_EXPANDER_PIN, false);
    gpio_set_drive_strength(TCA_EXPANDER_PIN, GPIO_DRIVE_STRENGTH_2MA);
#endif // TCA_EXPANDER

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

#if TCA_EXPANDER
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
#endif // TCA_EXPANDER
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

#if TCA_EXPANDER
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
#endif // TCA_EXPANDER
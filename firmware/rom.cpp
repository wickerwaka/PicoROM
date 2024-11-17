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

static constexpr uint SM_DATA = 0;
static constexpr uint SM_OE = 1;
static constexpr uint SM_REPORT = 2;
static constexpr uint SM_TCA = 3;

static void rom_pio_init_output_program()
{
    // Set the output direction
    pio_sm_set_consecutive_pindirs(data_pio, SM_DATA, BASE_DATA_PIN, N_DATA_PINS, true);

    uint offset = pio_add_program(data_pio, &output_program);
    pio_sm_config cfg = output_program_get_default_config(offset);

    // Set output pins and autopull at 8-bits
    sm_config_set_out_pins(&cfg, BASE_DATA_PIN, N_DATA_PINS);
    sm_config_set_out_shift(&cfg, true, true, N_DATA_PINS);
    pio_sm_init(data_pio, SM_DATA, offset, &cfg);
    pio_sm_set_enabled(data_pio, SM_DATA, true);
}

static void rom_pio_init_output_enable_program()
{
    // set oe pin directions, data pin direction will be set by the state machine
    pio_sm_set_consecutive_pindirs(data_pio, SM_OE, BASE_OE_PIN, N_OE_PINS, false);
    pio_sm_set_consecutive_pindirs(data_pio, SM_OE, BASE_BUF_OE_PIN, N_BUF_OE_PINS, true);


    uint offset = pio_add_program(data_pio, &output_enable_buffer_program);
    pio_sm_config cfg = output_enable_buffer_program_get_default_config(offset);

    // OE pins as input
    sm_config_set_in_pins(&cfg, BASE_OE_PIN);
    
    // Data pins as output, but just the direction is set
    sm_config_set_out_pins(&cfg, BASE_DATA_PIN, N_DATA_PINS);

    // We set the BUF_OE pin using the SET op
    sm_config_set_set_pins(&cfg, BASE_BUF_OE_PIN, N_BUF_OE_PINS);
    
    pio_sm_init(data_pio, SM_OE, offset, &cfg);
    pio_sm_set_enabled(data_pio, SM_OE, true);
}

static void rom_pio_init_output_enable_report_program()
{
    uint offset = pio_add_program(data_pio, &output_enable_report_program);
    pio_sm_config cfg = output_enable_report_program_get_default_config(offset);
    // This program looks at all input pins
    sm_config_set_in_pins(&cfg, 0);

    // Disable interrupts, we manually check the flag and clear it
    pio_set_irq0_source_enabled(data_pio, (enum pio_interrupt_source) ((uint) pis_interrupt0 + SM_REPORT), false);
    pio_set_irq1_source_enabled(data_pio, (enum pio_interrupt_source) ((uint) pis_interrupt0 + SM_REPORT), false);
    pio_interrupt_clear(data_pio, SM_REPORT);

    pio_sm_init(data_pio, SM_REPORT, offset, &cfg);
    pio_sm_set_enabled(data_pio, SM_REPORT, true);
}

static void rom_pio_init_tca_program()
{
    // Enable output and set pin high
    pio_sm_set_pindirs_with_mask(data_pio, SM_TCA, 0xffffffff, TCA_EXPANDER_PIN_MASK);
    pio_sm_set_pins_with_mask(data_pio, SM_TCA, 0xffffffff, TCA_EXPANDER_PIN_MASK);

    uint offset = pio_add_program(data_pio, &tca5405_program);
    pio_sm_config cfg = tca5405_program_get_default_config(offset);
    sm_config_set_out_pins(&cfg, TCA_EXPANDER_PIN, 1);
    sm_config_set_clkdiv(&cfg, 1000); // divide down to TCA rate
    sm_config_set_out_shift(&cfg, true, true, 10); // 4-bits of preample, 5-bits of data, 1-end bit 

    pio_sm_init(data_pio, SM_TCA, offset, &cfg);
    pio_sm_set_enabled(data_pio, SM_TCA, true);
}

void rom_init_programs()
{
    // Assign data and oe pins to pio
    for( uint ofs = 0; ofs < N_DATA_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_DATA_PIN + ofs);
        gpio_set_dir(BASE_DATA_PIN + ofs, true);
        gpio_set_drive_strength(BASE_DATA_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_input_enabled(BASE_DATA_PIN + ofs, false);
        gpio_set_inover(BASE_DATA_PIN + ofs, GPIO_OVERRIDE_LOW);
    }

    for( uint ofs = 0; ofs < N_OE_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_OE_PIN + ofs);
        gpio_set_dir(BASE_OE_PIN + ofs, false);
    }

    for( uint ofs = 0; ofs < N_BUF_OE_PINS; ofs++ )
    {
        pio_gpio_init(data_pio, BASE_BUF_OE_PIN + ofs);
        gpio_set_drive_strength(BASE_BUF_OE_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_input_enabled(BASE_BUF_OE_PIN + ofs, false);
        gpio_set_inover(BASE_BUF_OE_PIN + ofs, GPIO_OVERRIDE_LOW);
    }

    pio_gpio_init(data_pio, TCA_EXPANDER_PIN);
    gpio_set_input_enabled(TCA_EXPANDER_PIN, false);
    gpio_set_inover(TCA_EXPANDER_PIN, GPIO_OVERRIDE_LOW);
    gpio_set_drive_strength(TCA_EXPANDER_PIN, GPIO_DRIVE_STRENGTH_2MA);

    pio_sm_set_consecutive_pindirs(data_pio, SM_DATA, BASE_DATA_PIN, N_DATA_PINS, true);

    rom_pio_init_output_program();
    rom_pio_init_output_enable_program();
    rom_pio_init_output_enable_report_program();
    rom_pio_init_tca_program();

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
    if( pio_interrupt_get(data_pio, SM_REPORT) )
    {
        pio_interrupt_clear(data_pio, SM_REPORT);
        return true;
    }
    return false;
}

static uint8_t tca_pins_state = 0x0;
void tca_set_pins(uint8_t pins)
{
    uint32_t bitstream = 0b1000001010 | ((pins & 0x1f) << 4);
    data_pio->txf[SM_TCA] = bitstream;
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


#include "hardware/structs/bus_ctrl.h"
#include "hardware/structs/syscfg.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"

#include "pio_programs.h"
#include "rom.h"
#include "system.h"

uint8_t *rom_data = (uint8_t *)0x21000000; // Start of 4 64kb sram banks

uint32_t core1_stack[8];
static void __attribute__((noreturn, section(".time_critical.core1_rom_loop"))) rom_loop()
{
    register uint32_t r0 __asm__("r0") = (uint32_t)rom_data;
    register uint32_t r1 __asm__("r1") = ADDR_MASK;
    register uint32_t r2 __asm__("r2") = (uint32_t)&prg_data_output.pio()->txf[prg_data_output.sm];

#if defined(FEATURE_STABLE_ADDRESS)
    __asm__ volatile(
          "ldr r5, =0xd0000004 \n\t"
          "loop:               \n\t" //                         Cycles  Loop1 Loop2
          "ldr r3, [r5]        \n\t" // Read GPIO in r3         1       1     12
          "and r3, r1          \n\t" // AND with ADDR_MASK      1       2     13
          "ldr r4, [r5]        \n\t" // Read GPIO in r4         1       3     14
          "and r4, r1          \n\t" // AND with ADDR_MASK      1       4     15
          "cmp r4, r3          \n\t" //                         1       5     16
          "bne loop            \n\t" //                         1       6     17
          "ldrb r3, [r0, r3]   \n\t" // Read from rom_data      2       8     19
          "strb r3, [r2]       \n\t" // Write to FIFO           1       9     20
          "b loop              \n\t" // Loop                    2       11
          : "+r"(r0), "+r"(r1), "+r"(r2)
          :
          : "r4", "r5", "cc", "memory");
#else
    __asm__ volatile(
          "ldr r5, =0xd0000004 \n\t"
          "loop:               \n\t" //                         Cycles  Loop1 Loop2
          "ldr r3, [r5]        \n\t" // Read GPIO in r3         1       1     8
          "and r3, r1          \n\t" // AND with ADDR_MASK      1       2     9
          "ldrb r3, [r0, r3]   \n\t" // Read from rom_data      2       4     11
          "strb r3, [r2]       \n\t" // Write to FIFO           1       5     12
          "b loop              \n\t" // Loop                    2       7
          : "+r"(r0), "+r"(r1), "+r"(r2)
          :
          : "r5", "cc", "memory");
#endif
    __builtin_unreachable();
}


static void rom_pio_init_output_program()
{
    if (prg_data_output.valid())
    {
        PRG_LOCAL(prg_data_output, p, sm, offset, cfg);
        // Set the output direction
        pio_sm_set_consecutive_pindirs(p, sm, BASE_DATA_PIN, N_DATA_PINS, true);

        // Set output pins and autopull at 8-bits
        sm_config_set_out_pins(&cfg, BASE_DATA_PIN, N_DATA_PINS);
        sm_config_set_out_shift(&cfg, true, true, N_DATA_PINS);
        pio_sm_init(p, sm, offset, &cfg);
        pio_sm_set_enabled(p, sm, true);
    }
}

static void rom_pio_init_output_enable_program()
{
    if (prg_set_output_enable.valid())
    {
        PRG_LOCAL(prg_set_output_enable, p, sm, offset, cfg);

        // set oe pin directions, data pin direction will be set by the state machine
        pio_sm_set_consecutive_pindirs(p, sm, BASE_OE_PIN, N_OE_PINS, false);
        pio_sm_set_consecutive_pindirs(p, sm, BUF_OE_PIN, 1, true);

        // OE pins as input
        sm_config_set_in_pins(&cfg, BASE_OE_PIN);
        sm_config_set_sideset_pins(&cfg, BUF_OE_PIN);
        sm_config_set_out_pins(&cfg, BUF_OE_PIN, 1);

        pio_sm_init(p, sm, offset, &cfg);
        pio_sm_set_enabled(p, sm, true);
    }
}

static void rom_pio_init_pindirs_program()
{
    if (prg_set_pindir_lo.valid())
    {
        PRG_LOCAL(prg_set_pindir_lo, p, sm, offset, cfg);

        // OE pins as input
        sm_config_set_in_pins(&cfg, BASE_OE_PIN);
        sm_config_set_sideset_pins(&cfg, BASE_DATA_PIN);

        pio_sm_init(p, sm, offset, &cfg);
        pio_sm_set_enabled(p, sm, true);
    }

    if (prg_set_pindir_hi.valid())
    {
        PRG_LOCAL(prg_set_pindir_hi, p, sm, offset, cfg);

        // OE pins as input
        sm_config_set_in_pins(&cfg, BASE_OE_PIN);
        sm_config_set_sideset_pins(&cfg, BASE_DATA_PIN + 4);

        pio_sm_init(p, sm, offset, &cfg);
        pio_sm_set_enabled(p, sm, true);
    }
}


static void rom_pio_init_output_enable_report_program()
{
    if (prg_report_data_access.valid())
    {
        PRG_LOCAL(prg_report_data_access, p, sm, offset, cfg);

        sm_config_set_in_pins(&cfg, BASE_OE_PIN);

        // Disable interrupts, we manually check the flag and clear it
        pio_set_irq0_source_enabled(p, (enum pio_interrupt_source)((uint)pis_interrupt0 + sm), false);
        pio_set_irq1_source_enabled(p, (enum pio_interrupt_source)((uint)pis_interrupt0 + sm), false);
        pio_interrupt_clear(p, sm);

        pio_sm_init(p, sm, offset, &cfg);
        pio_sm_set_enabled(p, sm, true);
    }
}

void rom_init_programs()
{
    // Assign data and oe pins to pio
    for (uint ofs = 0; ofs < N_DATA_PINS; ofs++)
    {
        pio_gpio_init(prg_data_output.pio(), BASE_DATA_PIN + ofs);
        gpio_set_drive_strength(BASE_DATA_PIN + ofs, GPIO_DRIVE_STRENGTH_2MA);
        gpio_set_input_enabled(BASE_DATA_PIN + ofs, false);
        gpio_set_inover(BASE_DATA_PIN + ofs, GPIO_OVERRIDE_LOW);
        gpio_set_slew_rate(BASE_DATA_PIN + ofs, GPIO_SLEW_RATE_FAST);
    }

    for (uint ofs = 0; ofs < N_OE_PINS; ofs++)
    {
        gpio_init(BASE_OE_PIN + ofs);
        gpio_set_dir(BASE_OE_PIN + ofs, false);
        gpio_set_input_hysteresis_enabled(BASE_OE_PIN + ofs, false);
        syscfg_hw->proc_in_sync_bypass |= 1 << (BASE_OE_PIN + ofs);
    }

    pio_gpio_init(prg_set_output_enable.pio(), BUF_OE_PIN);
    gpio_set_drive_strength(BUF_OE_PIN, GPIO_DRIVE_STRENGTH_2MA);
    gpio_set_input_enabled(BUF_OE_PIN, false);
    gpio_set_inover(BUF_OE_PIN, GPIO_OVERRIDE_LOW);
    gpio_set_slew_rate(BUF_OE_PIN, GPIO_SLEW_RATE_FAST);

#if defined(BOARD_28P)
    gpio_init(BUF_DIR_PIN);
    gpio_set_input_enabled(BUF_DIR_PIN, false);
    gpio_set_inover(BUF_DIR_PIN, GPIO_OVERRIDE_LOW);
    gpio_set_dir(BUF_DIR_PIN, true);
    gpio_put(BUF_DIR_PIN, false);
#endif

    rom_pio_init_output_program();

    // Not needed on newer revisions, data dir is always out
#if defined(BOARD_32P_TCA)
    rom_pio_init_pindirs_program();
#endif

    rom_pio_init_output_enable_program();
    rom_pio_init_output_enable_report_program();
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
    if (pio_interrupt_get(prg_report_data_access.pio(), prg_report_data_access.sm))
    {
        pio_interrupt_clear(prg_report_data_access.pio(), prg_report_data_access.sm);
        return true;
    }
    return false;
}



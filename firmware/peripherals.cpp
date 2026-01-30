#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/syscfg.h"

#include "pico/time.h"

#include <stdio.h>
#include <unistd.h>

#include "peripherals.h"
#include "pio_programs.h"
#include "system.h"
#include "str_util.h"
#include "rom.h"
#include "pico_link.h"

#if defined(BOARD_32P_TCA)
static uint8_t tca_pins_state = 0x0;
static void tca_set_pins(uint8_t pins)
{
    uint32_t bitstream = 0b1000001010 | ((pins & 0x1f) << 4);
    prg_write_tca_bits.pio()->txf[prg_write_tca_bits.sm] = bitstream;
    tca_pins_state = pins;
}

static void tca_set_pin(int pin, bool en)
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
#endif


static uint8_t identify_req = 0;

repeating_timer_t activity_timer;

static uint8_t identify_ack = 0;
static uint8_t activity_cycles = 0;
static uint8_t activity_duty = 0;
static uint8_t activity_count = 0;

static uint8_t link_cycles = 0;
static uint8_t link_duty = 0;
static uint8_t link_count = 0;

bool activity_timer_callback(repeating_timer_t * /*unused*/)
{
    if (activity_count >= activity_cycles)
    {
        bool rom_access = rom_check_oe();

        activity_cycles = 0;
        activity_duty = 0;

        if (rom_access)
        {
            activity_cycles = 5;
            activity_duty = 1;
        }

        activity_count = 0;
    }

    if (link_count >= link_cycles)
    {
        bool identify_req = identify_req != identify_ack;
        bool usb_activity = pl_check_activity();

        link_cycles = 0;
        link_duty = 0;

        if (identify_req)
        {
            identify_ack++;
            link_cycles = 100;
            link_duty = 90;
        }
        else if (usb_activity)
        {
            link_cycles = 20;
            link_duty = 10;
        }

        link_count = 0;
    }

#if defined(BOARD_32P_TCA)
    tca_set_pin(TCA_LINK_PIN, link_count < link_duty);
    tca_set_pin(TCA_READ_PIN, activity_count < activity_duty);
#else
    if (link_count < link_duty)
    {
        pwm_set_gpio_level(INFO_LED_PIN, 64);
    }
    else if (activity_count < activity_duty)
    {
        pwm_set_gpio_level(INFO_LED_PIN, 64);
    }
    else
    {
        pwm_set_gpio_level(INFO_LED_PIN, 0);
    }
#endif
    activity_count++;
    link_count++;

    return true;
}

ResetLevel current_reset = ResetLevel::Z;

ResetLevel reset_get()
{
    return current_reset;
}

void reset_set(ResetLevel level)
{
    switch (level)
    {
        case ResetLevel::Low:
#if defined(BOARD_32P_TCA)
            tca_set_pin(TCA_RESET_VALUE_PIN, false);
            tca_set_pin(TCA_RESET_PIN, true);
#else
            gpio_put(RESET_PIN, false);
#endif
            current_reset = ResetLevel::Low;
            break;

        case ResetLevel::High:
#if defined(BOARD_32P_TCA)
            tca_set_pin(TCA_RESET_VALUE_PIN, true);
            tca_set_pin(TCA_RESET_PIN, true);
#else
            gpio_put(RESET_PIN, false);
#endif
            current_reset = ResetLevel::High;
            break;

        default:
#if defined(BOARD_32P_TCA)
            tca_set_pin(TCA_RESET_PIN, false);
#else
            gpio_put(RESET_PIN, true);
#endif
            current_reset = ResetLevel::Z;
            break;
    }
}

void reset_to_string(ResetLevel level, char *s, size_t sz)
{
    switch (level)
    {
        case ResetLevel::Low:
            strcpyz(s, sz, "low");
            break;

        case ResetLevel::High:
            strcpyz(s, sz, "high");
            break;

        default:
            strcpyz(s, sz, "z");
            break;
    }
}

bool reset_from_string(const char *s, ResetLevel *level)
{
    if (streq(s, "low") || streq(s, "l"))
    {
        *level = ResetLevel::Low;
        return true;
    }
    else if (streq(s, "high") || streq(s, "h"))
    {
        *level = ResetLevel::High;
        return true;
    }
    else if (streq(s, "z"))
    {
        *level = ResetLevel::Z;
        return true;
    }

    return false;
}

void trigger_identify_led()
{
    identify_req += 5;
}

void peripherals_init()
{
#if defined(BOARD_32P_TCA)
    if (prg_write_tca_bits.valid())
    {
        PRG_LOCAL(prg_write_tca_bits, p, sm, offset, cfg);

        // Enable output and set pin high
        pio_sm_set_pindirs_with_mask(p, sm, 0xffffffff, TCA_EXPANDER_PIN_MASK);
        pio_sm_set_pins_with_mask(p, sm, 0xffffffff, TCA_EXPANDER_PIN_MASK);

        sm_config_set_out_pins(&cfg, TCA_EXPANDER_PIN, 1);
        sm_config_set_clkdiv(&cfg, 1000); // divide down to TCA rate
        sm_config_set_out_shift(&cfg, true, true, 10); // 4-bits of preample, 5-bits of data, 1-end bit

        pio_sm_init(p, sm, offset, &cfg);
        pio_sm_set_enabled(p, sm, true);
    }
    pio_gpio_init(prg_write_tca_bits.pio(), TCA_EXPANDER_PIN);
    gpio_set_input_enabled(TCA_EXPANDER_PIN, false);
    gpio_set_inover(TCA_EXPANDER_PIN, GPIO_OVERRIDE_LOW);
    gpio_set_drive_strength(TCA_EXPANDER_PIN, GPIO_DRIVE_STRENGTH_2MA);

    tca_set_pins(0x00);
    tca_set_pins(0x00);
#else
    gpio_set_function(INFO_LED_PIN, GPIO_FUNC_PWM);
    gpio_set_input_enabled(INFO_LED_PIN, false);
    gpio_set_inover(INFO_LED_PIN, GPIO_OVERRIDE_LOW);
    gpio_set_dir(INFO_LED_PIN, true);

    uint slice_num = pwm_gpio_to_slice_num(INFO_LED_PIN);
    pwm_set_wrap(slice_num, 254);
    pwm_set_gpio_level(INFO_LED_PIN, 0);
    pwm_set_enabled(slice_num, true);

    gpio_init(RESET_PIN);
    gpio_set_input_enabled(RESET_PIN, false);
    gpio_set_inover(RESET_PIN, GPIO_OVERRIDE_LOW);
    gpio_set_dir(RESET_PIN, true);
    gpio_put(RESET_PIN, false);
#endif
    
    add_repeating_timer_ms(10, activity_timer_callback, nullptr, &activity_timer);
}



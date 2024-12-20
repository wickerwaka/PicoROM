#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/structs/syscfg.h"
#include <string.h>
#include <utility>
#include <array>


static constexpr uint32_t DATA_MASK = 0x000000ff;
static constexpr uint32_t DATA_SHIFT = 0;

static constexpr uint32_t ADDR_MASK = 0x047fff00;
static constexpr uint32_t ADDR_SHIFT = 8;

static constexpr uint32_t CE_SHIFT = 27;
static constexpr uint32_t CE_MASK = 0x1 << CE_SHIFT;

static constexpr uint32_t OE_SHIFT = 28;
static constexpr uint32_t OE_MASK = 0x1 << OE_SHIFT;


template <int N> __force_inline void asm_delay()
{
    pico_default_asm_volatile( "nop" );
    asm_delay<N - 1>();
}

template <> __force_inline void asm_delay<0>()
{
}

uint32_t make_addr(uint32_t addr)
{
    uint32_t exp = ((addr & 0x00008000) << 3) | addr;

    return (exp << ADDR_SHIFT) & ADDR_MASK;
}

void rom_configure_pins()
{
    gpio_init_mask(DATA_MASK | ADDR_MASK | CE_MASK | OE_MASK);

    // Direction
    gpio_set_dir_out_masked(ADDR_MASK | CE_MASK | OE_MASK);
    gpio_set_dir_in_masked(DATA_MASK);
    for( int pin = 0; pin < 31; pin++ )
    {
        uint32_t pin_mask = 1 << pin;

        if( pin_mask & DATA_MASK )
        {
            gpio_set_input_hysteresis_enabled(pin, false);
            syscfg_hw->proc_in_sync_bypass |= 1 << pin;
            gpio_set_pulls(pin, false, true);
        }

        gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
        //gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    }
}

template<int N> uint8_t rom_read(uint32_t address)
{
    uint32_t out = make_addr(address);
    gpio_put_masked(ADDR_MASK | CE_MASK | OE_MASK, out);
    asm_delay<N>();
    uint32_t v = gpio_get_all();
    return (v & DATA_MASK) >> DATA_SHIFT;
}

template<int N> uint8_t rom_read_ce(uint32_t address)
{
    uint32_t out = make_addr(address);
    gpio_put_masked(ADDR_MASK | CE_MASK | OE_MASK, out | OE_MASK);
    busy_wait_at_least_cycles(200);
    gpio_put_masked(ADDR_MASK | CE_MASK | OE_MASK, out);
    asm_delay<N>();
    uint32_t v = gpio_get_all();
    return (v & DATA_MASK) >> DATA_SHIFT;
}


template <int... Ns>
constexpr auto func_array_read_ce(std::integer_sequence<int, Ns...>) {
    return std::array<uint8_t(*)(uint32_t), sizeof...(Ns)>{&rom_read_ce<Ns>...};
}

template <int... Ns>
constexpr auto func_array_read(std::integer_sequence<int, Ns...>) {
    return std::array<uint8_t(*)(uint32_t), sizeof...(Ns)>{&rom_read<Ns>...};
}

uint8_t rom_read(uint32_t address, int delay)
{
    constexpr auto func_array = func_array_read(std::make_integer_sequence<int, 64>{});
    return func_array[delay](address);
}

uint8_t rom_read_ce(uint32_t address, int delay)
{
    constexpr auto func_array = func_array_read_ce(std::make_integer_sequence<int, 64>{});
    return func_array[delay](address);
}

uint32_t data[16 * 1024];

int main()
{
    uint8_t results[256];
    stdio_init_all();

    set_sys_clock_khz(270000, true);

    rom_configure_pins();

    bool ce_tests = false;

    while (true)
    {
        int fail_count = 0;
        int succeed_count = 0;
        //int delay = 30;
        for( int delay = 5; delay < 25; delay++ )
        {
            bool all_valid = true;
            for( int h = 0; h < 256; h++ )
            {
                for( int i = 0; i < 256; i++ )
                {
                    if (ce_tests)
                        results[i] = rom_read_ce( h << 8 | i, delay);
                    else
                        results[i] = rom_read( h << 8 | i, delay);
                }

                for( int i = 0; i < 256; i++ )
                {
                    if( results[i] != ( i ^ h ) )
                    {
                        all_valid = false;
                        //printf( "%02x != %02x\n", results[i], i ^ h);
                    }
                }
            }

            if (!all_valid)
            {
                printf( "[%s] FAIL with %d delay cycles.\n", ce_tests ? "CE READ" : "READ", delay );
                fail_count++;
            }
            else
            {
                printf( "[%s] PASS with %d delay cycles.\n", ce_tests ? "CE READ" : "READ", delay );
                succeed_count++;
            }
        }

        sleep_ms(1000);

        ce_tests = !ce_tests;
    }
}
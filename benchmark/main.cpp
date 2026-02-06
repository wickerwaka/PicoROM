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

void set_data_pulls(bool up)
{
    for( int pin = 0; pin < 31; pin++ )
    {
        uint32_t pin_mask = 1 << pin;

        if( pin_mask & DATA_MASK )
        {
            gpio_set_pulls(pin, up, ~up);
        }
    }
}

static inline uint32_t make_addr(uint32_t addr)
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
        }

        gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
        //gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_12MA);
    }

    set_data_pulls(false);
}

static inline void address_bus(uint32_t address, bool ce, bool oe)
{
    uint32_t out = make_addr(address);
    gpio_put_masked(ADDR_MASK | CE_MASK | OE_MASK, out | (ce ? 0 : CE_MASK) | (oe ? 0 : OE_MASK));
}

static inline uint8_t read_data()
{
    uint32_t v = gpio_get_all();
    return (v & DATA_MASK) >> DATA_SHIFT;
}

template<int N> uint8_t rom_read(uint32_t address)
{
    address_bus(address, true, true);
    asm_delay<N>();
    return read_data();
}

template<int N> uint8_t rom_read_ce(uint32_t address)
{
    address_bus(address, true, false);
    busy_wait_at_least_cycles(200);
    address_bus(address, true, true);
    asm_delay<N>();
    return read_data();
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

void test_disabled()
{
    for( int mode = 0; mode < 7; mode++ )
    {
        bool ce, oe, pullup;
        switch(mode)
        {
            case 0: ce = false; oe = false; pullup = false; break;
            case 1: ce = false; oe = false; pullup = true; break;
            case 2: ce = false; oe = true; pullup = false; break;
            case 3: ce = false; oe = true; pullup = true; break;
            case 4: ce = true; oe = false; pullup = false; break;
            case 5: ce = true; oe = false; pullup = true; break;
            case 6: ce = true; oe = true; pullup = true; break;
        }

        set_data_pulls(pullup);

        int fail_count = 0;
        for( int addr = 0; addr < 1024; addr++ )
        {
            address_bus(addr, ce, oe);
            busy_wait_at_least_cycles(200);
            uint8_t d = read_data();
            printf("%02X", d);
            if (d != (pullup ? 0xff : 0x00)) fail_count++;
        }

        printf("\n[%s] CE: %s  OE: %s  PULLUP:  %s\n",
                fail_count == 0 ? "PASS" : "FAIL",
                ce ? "SET" : "CLR",
                oe ? "SET" : "CLR",
                pullup ? "HI " : "LOW");
    }
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
        test_disabled();
/*
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
*/
        sleep_ms(1000);

        ce_tests = !ce_tests;
    }
}

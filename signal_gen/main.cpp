#include <stdio.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"

static constexpr uint32_t BASE_DATA_PIN = 22;
static constexpr uint32_t BUF_OE_PIN = 19;

int main()
{
    for( uint ofs = 0; ofs < 8; ofs++ )
    {
        uint gpio = BASE_DATA_PIN + ofs;
        
        gpio_init(gpio);
        gpio_set_dir(gpio, true);
    }

    gpio_init(BUF_OE_PIN);
    gpio_set_dir(BUF_OE_PIN, true);

    stdio_init_all();

    gpio_put(BUF_OE_PIN, false);

    uint32_t addr = 0;

    while( true )
    {
        gpio_put_masked(0xff << BASE_DATA_PIN, addr << BASE_DATA_PIN);
        addr++;
    }
}
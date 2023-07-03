#include <stdio.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/structs/syscfg.h"
#include "pico/binary_info.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"

#include <tusb.h>

#include "pico_link.h"

#include "data_bus.pio.h"

static constexpr uint N_DATA_PINS = 8;
static constexpr uint N_OE_PINS = 2;
static constexpr uint N_ADDR_PINS = 19;
static constexpr uint BASE_DATA_PIN = 22;
static constexpr uint BASE_OE_PIN = 20;
static constexpr uint BASE_ADDR_PIN = 0;

static constexpr uint ROM_SIZE = 0x40000;
static constexpr uint ADDR_MASK = ROM_SIZE - 1;
static constexpr uint FLASH_SIZE = 2 * 1024 * 1024;

static constexpr uint FLASH_ROM_OFFSET = FLASH_SIZE - ROM_SIZE;
static constexpr uint FLASH_CFG_OFFSET = FLASH_ROM_OFFSET - FLASH_SECTOR_SIZE;

static constexpr uint CONFIG_VERSION = 0x00010006;

uint32_t rom_offset = 0;
uint8_t *rom_data = (uint8_t *)0x21000000; // Start of 4 64kb sram banks
const uint8_t *flash_rom_data = (uint8_t *)(XIP_BASE + FLASH_ROM_OFFSET);

struct Config
{
    uint32_t version;
    char name[32];

    uint8_t _padding[256-36];
};

const Config *config = (Config *)(XIP_BASE + FLASH_CFG_OFFSET);
static_assert(sizeof(Config) == FLASH_PAGE_SIZE);

void init_data_bus_programs(PIO pio, uint sm_data, uint sm_oe)
{
    // Assign data and oe pins to pio
    for( uint ofs = 0; ofs < N_DATA_PINS; ofs++ )
    {
        pio_gpio_init(pio, BASE_DATA_PIN + ofs);
    }

    for( uint ofs = 0; ofs < N_OE_PINS; ofs++ )
    {
        pio_gpio_init(pio, BASE_OE_PIN + ofs);
    }

    // set oe pin directions, data pin direction will be set by the sm
    pio_sm_set_consecutive_pindirs(pio, sm_oe, BASE_OE_PIN, N_OE_PINS, false);

    // set out/in bases
    uint offset_data = pio_add_program(pio, &output_program);
    pio_sm_config c_data = output_program_get_default_config(offset_data);

    sm_config_set_out_pins(&c_data, BASE_DATA_PIN, N_DATA_PINS);
    sm_config_set_out_shift(&c_data, true, true, N_DATA_PINS);
    pio_sm_init(pio, sm_data, offset_data, &c_data);
    pio_sm_set_enabled(pio, sm_data, true);

    uint offset_oe = pio_add_program(pio, &output_enable_program);
    pio_sm_config c_oe = output_enable_program_get_default_config(offset_oe);
    sm_config_set_in_pins(&c_oe, BASE_OE_PIN);
    sm_config_set_out_pins(&c_oe, BASE_DATA_PIN, N_DATA_PINS);

    pio_sm_init(pio, sm_oe, offset_oe, &c_oe);

    pio_sm_set_enabled(pio, sm_oe, true);
}

extern "C" void __attribute__((noreturn)) read_handler(void *rom_base, uint32_t addr_mask, io_wo_32 *tx_fifo);
uint32_t core1_stack[8];

void __attribute__((noreturn)) core1_entry()
{
    read_handler(rom_data, ADDR_MASK, &pio0->txf[0]);
}

void init_config()
{
    if (config->version == CONFIG_VERSION) return;

    Config cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.version = CONFIG_VERSION;

    pico_get_unique_board_id_string(cfg.name, sizeof(cfg.name));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CFG_OFFSET, (uint8_t *)&cfg, sizeof(cfg));
    restore_interrupts(ints);
}

void save_config(const char *name, int len)
{
    Config cfg;
    memcpy(&cfg, config, sizeof(Config));

    cfg.version = CONFIG_VERSION;
    //if (len == -1) len = strlen(name);
    //if (len >= sizeof(cfg.name)) len = sizeof(cfg.name) - 1;
    memcpy(cfg.name, name, len);
    cfg.name[len] = 0;

    if( !memcmp(&cfg, config, sizeof(Config))) return;

    multicore_reset_core1();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CFG_OFFSET, (uint8_t *)&cfg, sizeof(cfg));
    restore_interrupts(ints);
    multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));
}

void save_rom()
{
    multicore_reset_core1();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_ROM_OFFSET, ROM_SIZE);
    flash_range_program(FLASH_ROM_OFFSET, rom_data, ROM_SIZE);
    restore_interrupts(ints);
    multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));
}




int main()
{
    stdio_init_all();

    init_config();

    set_sys_clock_khz(160000, true);

    // configure address lines
    for( uint gpio = BASE_ADDR_PIN; gpio < N_ADDR_PINS + BASE_ADDR_PIN; gpio++ )
    {
        gpio_init(gpio);
        gpio_set_pulls(gpio, false, false);
        gpio_set_input_enabled(gpio, true);
        gpio_set_input_hysteresis_enabled(gpio, false);
        syscfg_hw->proc_in_sync_bypass |= 1 << gpio;
    }

    for( uint i = 0; i < ROM_SIZE; i++ )
    {
        rom_data[i] = flash_rom_data[i];
    }

    uint sm_data = pio_claim_unused_sm(pio0, true);
    uint sm_oe = pio_claim_unused_sm(pio0, true);
    init_data_bus_programs(pio0, sm_data, sm_oe);

    multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));

    while (true)
    {
        // Reset state
        rom_offset = 0;

        pl_wait_for_connection();

        pl_send_debug("Connected", 1, 2);

        // Loop while connected
        while (pl_is_connected())
        {
            const Packet *req = pl_poll();
            if (req)
            {
                switch((PacketType)req->type)
                {
                    case PacketType::IdentReq:
                    {
                        pl_send_string(PacketType::IdentResp, config->name);
                        break;
                    }

                    case PacketType::IdentSet:
                    {
                        save_config((const char *)req->payload, req->size);
                        break;
                    }

                    case PacketType::SetPointer:
                    {
                        memcpy(&rom_offset, req->payload, sizeof(uint32_t));
                        break;
                    }

                    case PacketType::GetPointer:
                    {
                        pl_send_payload(PacketType::CurPointer, &rom_offset, sizeof(rom_offset));
                        break;
                    }

                    case PacketType::Write:
                    {
                        uint32_t offset = rom_offset;
                        if ((offset + req->size) > ROM_SIZE)
                        {
                            pl_send_error("Write out of range", offset, req->size);
                            break;
                        }
                        memcpy(rom_data + offset, req->payload, req->size);
                        rom_offset += req->size;
                        break;
                    }

                    case PacketType::Read:
                    {
                        uint32_t offset = rom_offset;
                        uint32_t size = MIN(MAX_PKT_PAYLOAD, ROM_SIZE - offset);
                        pl_send_payload(PacketType::ReadData, rom_data + offset, size);
                        rom_offset += size;
                        break;
                    }

                    case PacketType::CommitFlash:
                    {
                        save_rom();
                        break;
                    }

                    default:
                    {
                        pl_send_error("Unrecognized packet", req->type, req->size);
                        break;
                    }
                }
                pl_consume_packet(req);
            }
        }
    }
}

#include <stdio.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/structs/syscfg.h"
#include "pico/binary_info.h"
#include "pico/unique_id.h"

#include <tusb.h>

#include "system.h"
#include "pico_link.h"
#include "rom.h"
#include "comms.h"


static constexpr uint FLASH_ROM_OFFSET = FLASH_SIZE - ROM_SIZE;
static constexpr uint FLASH_CFG_OFFSET = FLASH_ROM_OFFSET - FLASH_SECTOR_SIZE;

static constexpr uint CONFIG_VERSION = 0x00010007;

uint32_t rom_offset = 0;
const uint8_t *flash_rom_data = (uint8_t *)(XIP_BASE + FLASH_ROM_OFFSET);

struct Config
{
    uint32_t version;
    char name[32];

    uint32_t addr_mask;
};

Config config;
const Config *flash_config = (Config *)(XIP_BASE + FLASH_CFG_OFFSET);
static_assert(sizeof(Config) <= FLASH_PAGE_SIZE);



void save_config()
{
    if( !memcmp(&config, flash_config, sizeof(Config))) return;

    rom_service_stop();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CFG_OFFSET, (uint8_t *)&config, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    rom_service_start();
}


void init_config()
{
    memcpy(&config, flash_config, sizeof(Config));

    if (config.version == CONFIG_VERSION) return;

    memset(&config, 0, sizeof(Config));

    config.addr_mask = ADDR_MASK;
    config.version = CONFIG_VERSION;
    pico_get_unique_board_id_string(config.name, sizeof(config.name));

    save_config();
}


void save_rom()
{
    rom_service_stop();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_ROM_OFFSET, ROM_SIZE);
    flash_range_program(FLASH_ROM_OFFSET, rom_get_buffer(), ROM_SIZE);
    restore_interrupts(ints);
    rom_service_start();
}


void configure_address_pins(uint32_t mask)
{
    mask &= ADDR_MASK;

    // configure address lines
    for( uint ofs = 0; ofs < N_ADDR_PINS; ofs++ )
    {
        uint gpio = BASE_ADDR_PIN + ofs;
        
        gpio_init(gpio);
        gpio_set_pulls(gpio, false, true);
        gpio_set_input_hysteresis_enabled(gpio, false);
        syscfg_hw->proc_in_sync_bypass |= 1 << gpio;

        if (mask & (1 << ofs))
        {
            gpio_set_input_enabled(gpio, true);
        }
        else
        {
            gpio_set_input_enabled(gpio, false);
        }
    }
}

static uint32_t identify_request = 0;

#if ACTIVITY_LED==1
repeating_timer_t activity_timer;

static uint32_t identify_ack = 0;
static uint8_t activity_cycles = 0;
static uint8_t activity_duty = 0;
static uint8_t activity_count = 0;

bool activity_timer_callback(repeating_timer_t * /*unused*/)
{
    if (activity_count >= activity_cycles)
    {
        bool rom_access = rom_check_oe();
        bool usb_connected = pl_is_connected();
        bool identify_req = identify_request != identify_ack;

        activity_cycles = 0;
        activity_duty = 0;
        
        if (identify_req)
        {
            identify_ack++;
            activity_cycles = 10;
            activity_duty = 8;
        }
        else if (rom_access)
        {
            activity_cycles = 3;
            activity_duty = 1;
        }

        activity_count = 0;
    }

    if (activity_count >= activity_duty)
    {
        gpio_put(ACTIVITY_LED_PIN, false);
    }
    else
    {
        gpio_put(ACTIVITY_LED_PIN, true);
    }

    activity_count++;

    return true;
}
#endif // ACTIVITY_LED==1

int main()
{
    tusb_init();

    init_config();

    set_sys_clock_khz(160000, true);

    configure_address_pins(config.addr_mask);

#if ACTIVITY_LED==1
    identify_ack = identify_request = 0;

    gpio_init(ACTIVITY_LED_PIN);
    gpio_set_dir(ACTIVITY_LED_PIN, true);
    gpio_set_input_enabled(ACTIVITY_LED_PIN, false);

    add_repeating_timer_ms(100, activity_timer_callback, nullptr, &activity_timer);
#endif

    memcpy(rom_get_buffer(), flash_rom_data, ROM_SIZE);

    rom_init_programs();

    comms_init();

    rom_service_start();

    while (true)
    {
        // Reset state
        rom_offset = 0;
        comms_end_session();

        pl_wait_for_connection();

        pl_send_debug("Connected", 1, 2);

        // Loop while connected
        while (pl_is_connected())
        {
            uint32_t addr = sio_hw->gpio_in & config.addr_mask;
            if( !comms_update(nullptr, 0, 5000) )
            {
                pl_send_error("Comms Update Timeout", 0, 0);
            }

            const Packet *req = pl_poll();

            if (req)
            {
                switch((PacketType)req->type)
                {
                    case PacketType::IdentReq:
                    {
                        pl_send_string(PacketType::IdentResp, config.name);
                        break;
                    }

                    case PacketType::IdentSet:
                    {
                        memcpy(config.name, req->payload, req->size);
                        config.name[req->size] = '\0';
                        save_config();
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
                        memcpy(rom_get_buffer() + offset, req->payload, req->size);
                        rom_offset += req->size;
                        break;
                    }

                    case PacketType::Read:
                    {
                        uint32_t offset = rom_offset;
                        uint32_t size = MIN(MAX_PKT_PAYLOAD, ROM_SIZE - offset);
                        pl_send_payload(PacketType::ReadData, rom_get_buffer() + offset, size);
                        rom_offset += size;
                        break;
                    }

                    case PacketType::CommitFlash:
                    {
                        save_rom();
                        pl_send_null(PacketType::CommitDone);
                        break;
                    }

                    case PacketType::CommsStart:
                    {
                        uint32_t addr;
                        memcpy(&addr, req->payload, 4);
                        comms_begin_session(addr, rom_get_buffer());
                        pl_send_debug("Comms Started", addr, 0);
                        break;
                    }

                    case PacketType::CommsEnd:
                    {
                        comms_end_session();
                        pl_send_debug("Comms Ended", 0, 0);
                        break;
                    }

                    case PacketType::CommsData:
                    {
                        if( !comms_update(req->payload, req->size, 5000) )
                        {
                            pl_send_error("Comms send timeout", 0, 0);
                        }
                        break;
                    }

                    case PacketType::SetMask:
                    {
                        uint32_t mask;
                        memcpy(&mask, req->payload, 4);
                        config.addr_mask = mask;
                        configure_address_pins(mask);
                        break;
                    }

                    case PacketType::GetMask:
                    {
                        pl_send_payload(PacketType::CurMask, &config.addr_mask, 4);
                        break;
                    }

                    case PacketType::Identify:
                    {
                        identify_request += 5;
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

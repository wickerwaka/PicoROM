#include <stdio.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/flash.h"
#include "hardware/structs/syscfg.h"
#include "pico/binary_info.h"
#include "pico/unique_id.h"

#include <tusb.h>

#include "system.h"
#include "pico_link.h"
#include "rom.h"
#include "flash.h"
#include "comms.h"
#include "pio_programs.h"

bi_decl(bi_program_feature("Reset"));

uint32_t rom_offset = 0;

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

static uint8_t identify_request = 0;

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
        bool identify_req = identify_request != identify_ack;
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

    tca_set_pin(TCA_LINK_PIN, link_count < link_duty);
    tca_set_pin(TCA_READ_PIN, activity_count < activity_duty);

    activity_count++;
    link_count++;

    return true;
}

uint32_t flash_load_time = 0;

uint32_t system_status = 0;

int main()
{
    static Config config; // static because it can't be on the stack otherwise it will be at the end of memory and will fault when copying to flash memory

    set_sys_clock_khz(270000, true);

    flash_init_config(&config);
    flash_load_time = flash_load_rom();

    if( pio_programs_init() )
    {
        system_status |= STATUS_PIO_INIT;
    }

    tusb_init();

    configure_address_pins(config.addr_mask);

    identify_ack = identify_request = 0;

    add_repeating_timer_ms(10, activity_timer_callback, nullptr, &activity_timer);

    rom_init_programs();

    rom_service_start();

    while (true)
    {
        // Reset state
        rom_offset = 0;
        comms_end_session();

        pl_wait_for_connection();

        pl_send_debug("Connected", 1, 2);
        pl_send_debug("Flash Load Time", flash_load_time, system_status);

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
                        flash_save_config(&config);
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
                        flash_save_rom();
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

                    case PacketType::Reset:
                    {
                        switch(req->payload[0])
                        {
                            case 'L':
                                tca_set_pin(TCA_RESET_VALUE_PIN, false);
                                tca_set_pin(TCA_RESET_PIN, true);
                                break;

                            case 'H':
                                tca_set_pin(TCA_RESET_VALUE_PIN, true);
                                tca_set_pin(TCA_RESET_PIN, true);
                                break;

                            default:
                                tca_set_pin(TCA_RESET_PIN, false);
                                break;
                        }
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

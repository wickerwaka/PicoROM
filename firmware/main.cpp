#include <stdio.h>
#include <unistd.h>
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/structs/syscfg.h"
#include "pico/bootrom.h"

#include <tusb.h>

#include "system.h"
#include "pico_link.h"
#include "rom.h"
#include "flash.h"
#include "comms.h"
#include "pio_programs.h"
#include "str_util.h"


// TODO
// Change addr mask to config
//

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

ResetLevel current_reset = ResetLevel::Z;

void reset_set(ResetLevel level)
{
    switch(level)
    {
        case ResetLevel::Low:
            tca_set_pin(TCA_RESET_VALUE_PIN, false);
            tca_set_pin(TCA_RESET_PIN, true);
            current_reset = ResetLevel::Low;
            break;

        case ResetLevel::High:
            tca_set_pin(TCA_RESET_VALUE_PIN, true);
            tca_set_pin(TCA_RESET_PIN, true);
            current_reset = ResetLevel::High;
            break;

        default:
            tca_set_pin(TCA_RESET_PIN, false);
            current_reset = ResetLevel::Z;
            break;
    }
}

void reset_to_string(ResetLevel level, char *s, size_t sz)
{
    switch(level)
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

uint32_t flash_load_time = 0;
uint32_t system_status = 0;

static Config config;

static const char *parameter_names[] =
{
    "name",
    "rom_name",
    "addr_mask",
    "initial_reset",
    "default_reset",
    "reset",
    "status",
    "startup_time",
    "build_config",
    "build_version",
    nullptr
};

bool set_parameter(const char *name, const char *value)
{
    if (streq(name, "addr_mask"))
    {
        config.addr_mask = strtoul(value) & ADDR_MASK;
        configure_address_pins(config.addr_mask);
        return true;
    }
    else if (streq(name, "name"))
    {
        strcpyz(config.name, sizeof(config.name), value);
        flash_save_config(&config);
        return true;
    }
    else if (streq(name, "rom_name"))
    {
        strcpyz(config.rom_name, sizeof(config.name), value);
        return true;
    }
    else if (streq(name, "initial_reset"))
    {
        if (reset_from_string(value, &config.initial_reset))
        {
            flash_save_config(&config);
            return true;
        }
    }
    else if (streq(name, "default_reset"))
    {
        if (reset_from_string(value, &config.default_reset))
        {
            flash_save_config(&config);
            return true;
        }
    }
    else if (streq(name, "reset"))
    {
        ResetLevel level;
        if (reset_from_string(value, &level))
        {
            reset_set(level);
            return true;
        }
        else
        {
            return false;
        }
    }
    return false;
}

bool get_parameter(const char *name, char *value, size_t value_size)
{
    if (streq(name, "addr_mask"))
    {
        snprintf(value, value_size, "0x%08x", config.addr_mask);
        return true;
    }
    else if (streq(name, "name"))
    {
        strcpyz(value, value_size, config.name);
        return true;
    }
    else if (streq(name, "rom_name"))
    {
        strcpyz(value, value_size, config.rom_name);
        return true;
    }
    else if (streq(name, "status"))
    {
        snprintf(value, value_size, "0x%08x", system_status);
        return true;
    }
    else if (streq(name, "startup_time"))
    {
        snprintf(value, value_size, "%d", flash_load_time);
        return true;
    }
    else if (streq(name, "initial_reset"))
    {
        reset_to_string(config.initial_reset, value, value_size);
        return true;
    }
    else if (streq(name, "default_reset"))
    {
        reset_to_string(config.default_reset, value, value_size);
        return true;
    }
    else if (streq(name, "reset"))
    {
        reset_to_string(current_reset, value, value_size);
        return true;
    }
    else if (streq(name, "build_config"))
    {
        strcpyz(value, value_size, PICOROM_CONFIG_NAME );
        return true;
    }
    else if (streq(name, "build_version"))
    {
        strcpyz(value, value_size, PICOROM_FIRMWARE_VERSION );
        return true;
    }


    return false;
}

int main()
{
    set_sys_clock_khz(270000, true);

    flash_init_config(&config);

    if( pio_programs_init() )
    {
        system_status |= STATUS_PIO_INIT;
    }
    
    rom_init_programs();

    reset_set(config.initial_reset);
    
    flash_load_time = flash_load_rom();

    reset_set(config.default_reset);

    tusb_init();

    configure_address_pins(config.addr_mask);

    identify_ack = identify_request = 0;

    add_repeating_timer_ms(10, activity_timer_callback, nullptr, &activity_timer);

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
                        flash_save_config(&config);
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

                    case PacketType::SetParameter:
                    {
                        char *split = (char *)memchr(req->payload, ',', req->size);
                        if (split != nullptr)
                        {
                            *split = '\0';
                            if (set_parameter((char *)req->payload, split + 1))
                            {
                                Packet pkt;
                                if (get_parameter((const char *)req->payload, (char *)pkt.payload, sizeof(pkt.payload)))
                                {
                                    pkt.size = strlen((char *)pkt.payload);
                                    pkt.type = (uint8_t)PacketType::Parameter;
                                    pl_send_packet(&pkt);
                                }
                                else
                                {
                                    pl_send_null(PacketType::ParameterError);
                                }
                                break;
                            }
                            else
                            {
                                pl_send_null(PacketType::ParameterError);
                            }
                        }
                        else
                        {
                            pl_send_null(PacketType::ParameterError);
                        }
                        break;
                    }

                    case PacketType::GetParameter:
                    {
                        Packet pkt;
                        if (get_parameter((const char *)req->payload, (char *)pkt.payload, sizeof(pkt.payload)))
                        {
                            pkt.size = strlen((char *)pkt.payload);
                            pkt.type = (uint8_t)PacketType::Parameter;
                        }
                        else
                        {
                            pkt.size = 0;
                            pkt.type = (uint8_t)PacketType::ParameterError;
                        }
                        pl_send_packet(&pkt);
                        break;
                    }

                    case PacketType::QueryParameter:
                    {
                        if (req->size == 0)
                        {
                            pl_send_string(PacketType::Parameter, parameter_names[0]);
                        }
                        else
                        {
                            const char **p = parameter_names;
                            while(p)
                            {
                                if (!strcmp(*p, (char *)req->payload))
                                {
                                    p++;
                                    break;
                                }
                                p++;
                            }

                            if (p)
                                pl_send_string(PacketType::Parameter, *p);
                            else
                                pl_send_null(PacketType::Parameter);
                        }
                        break;
                    }

                    case PacketType::Identify:
                    {
                        identify_request += 5;
                        break;
                    }

                    case PacketType::Bootsel:
                    {
                        rom_reset_usb_boot(-1, 0);
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



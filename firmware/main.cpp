#include "hardware/gpio.h"
#include "hardware/structs/syscfg.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include <stdio.h>
#include <unistd.h>

#include <tusb.h>

#include "comms.h"
#include "flash.h"
#include "peripherals.h"
#include "pico_link.h"
#include "pio_programs.h"
#include "rom.h"
#include "str_util.h"
#include "system.h"


// Dummy atexit implementation because some SDK/newlib versions don't strip
// atexit and it uses several 100bytes of RAM
int __wrap_atexit(void *)
{
    return 0;
}

uint32_t rom_offset = 0;

void configure_address_pins(uint32_t mask)
{
    mask &= ADDR_MASK;

    // configure address lines
    for (uint ofs = 0; ofs < N_ADDR_PINS; ofs++)
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

uint32_t flash_load_time = 0;
uint32_t system_status = 0;

// Flag to trigger USB re-enumeration from main loop (not safe in USB callback)
static bool usb_reenumerate_pending = false;

static Config config;

static const char *parameter_names[] = {
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
        usb_reenumerate_pending = true;
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
        reset_to_string(reset_get(), value, value_size);
        return true;
    }
    else if (streq(name, "build_config"))
    {
        strcpyz(value, value_size, PICOROM_CONFIG_NAME);
        return true;
    }
    else if (streq(name, "build_version"))
    {
        strcpyz(value, value_size, PICOROM_FIRMWARE_VERSION);
        return true;
    }


    return false;
}

// Packet handler - called from USB RX callback
void handle_packet(const Packet *req)
{
    switch ((PacketType)req->type)
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
            if (!comms_update(req->payload, req->size, 5000))
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
                while (p)
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
            trigger_identify_led();
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
}

int main()
{
    flash_init_config(&config);

    if (pio_programs_init())
    {
        system_status |= STATUS_PIO_INIT;
    }

    rom_init_programs();

    reset_set(config.initial_reset);

    flash_load_time = flash_load_rom();

    tusb_init();

    pl_init(handle_packet);

    configure_address_pins(config.addr_mask);

    rom_service_start();

    peripherals_init();

    reset_set(config.default_reset);

    // Arm initial RX transfer
    tud_vendor_read_flush();

    while (true)
    {
        tud_task();

        if (usb_reenumerate_pending)
        {
            usb_reenumerate_pending = false;
            // Allow USB response to complete before disconnecting
            sleep_ms(50);
            tud_task();
            tud_disconnect();
            sleep_ms(250);
            tud_connect();
        }

        comms_update(nullptr, 0, 5000);
    }
}

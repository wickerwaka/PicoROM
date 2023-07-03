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

#include "data_bus.pio.h"

static constexpr uint N_DATA_PINS = 8;
static constexpr uint N_OE_PINS = 2;
static constexpr uint N_ADDR_PINS = 19;
static constexpr uint BASE_DATA_PIN = 22;
static constexpr uint BASE_OE_PIN = 20;
static constexpr uint BASE_ADDR_PIN = 0;

static constexpr uint MAX_ROM_SIZE = 0x40000;
static constexpr uint FLASH_SIZE = 2 * 1024 * 1024;

static constexpr uint FLASH_ROM_OFFSET = FLASH_SIZE - MAX_ROM_SIZE;
static constexpr uint FLASH_CFG_OFFSET = FLASH_ROM_OFFSET - FLASH_SECTOR_SIZE;

static constexpr uint CONFIG_VERSION = 0x00010005;

uint32_t rom_offset = 0;
uint8_t *rom_data = (uint8_t *)0x21000000; // Start of 4 64kb sram banks
const uint8_t *flash_rom_data = (uint8_t *)(XIP_BASE + FLASH_ROM_OFFSET);
uint32_t rom_size = MAX_ROM_SIZE;

struct Config
{
    uint version;
    char name[32];

    uint32_t rom_size;

    uint8_t _padding[256-40];
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
    read_handler(rom_data, config->rom_size - 1, &pio0->txf[0]);
}

void init_config()
{
    if (config->version == CONFIG_VERSION) return;

    Config cfg;
    memset(&cfg, 0, sizeof(cfg));

    cfg.version = CONFIG_VERSION;
    cfg.rom_size = MAX_ROM_SIZE;

    pico_get_unique_board_id_string(cfg.name, sizeof(cfg.name));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CFG_OFFSET, (uint8_t *)&cfg, sizeof(cfg));
    restore_interrupts(ints);
}

void save_config(const char *name, int len, uint32_t rom_size)
{
    Config cfg;
    memcpy(&cfg, config, sizeof(Config));

    cfg.version = CONFIG_VERSION;
    //if (len == -1) len = strlen(name);
    //if (len >= sizeof(cfg.name)) len = sizeof(cfg.name) - 1;
    memcpy(cfg.name, name, len);
    cfg.name[len] = 0;

    cfg.rom_size = rom_size;

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
    save_config(config->name, strlen(config->name), rom_size);

    multicore_reset_core1();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_ROM_OFFSET, MAX_ROM_SIZE);
    flash_range_program(FLASH_ROM_OFFSET, rom_data, MAX_ROM_SIZE);
    restore_interrupts(ints);
    multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));
}


enum class PacketType : uint8_t
{
    IdentReq = 0,
    IdentResp = 1,
    IdentSet = 2,

    SetPointer = 3,
    GetPointer = 4,
    CurPointer = 5,
    Write = 6,
    Read = 7,
    ReadData = 8,

    SetSize = 9,
    GetSize = 10,
    CurSize = 11,

    CommitFlash = 12,

    Debug = 0xff
};

static constexpr size_t MAX_PKT_PAYLOAD = 30;

struct Packet
{
    uint8_t type;
    uint8_t size;

    uint8_t payload[MAX_PKT_PAYLOAD];
};

void send_packet_null(PacketType type)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)type;
    buf[1] = 0;
    write(1, buf, 2);
}

void send_packet_string(PacketType type, const char *s)
{
    Packet pkt;
    pkt.type = (uint8_t)type;
    pkt.size = MIN(MAX_PKT_PAYLOAD, strlen(s));
    strncpy((char *)pkt.payload, s, pkt.size);
    write(1, &pkt, pkt.size + 2);
}

void send_packet(PacketType type, const void *data, size_t len)
{
    Packet pkt;
    pkt.type = (uint8_t)type;
    pkt.size = MIN(len, MAX_PKT_PAYLOAD);
    memcpy(pkt.payload, data, len);
    write(1, &pkt, pkt.size + 2);
}

void send_packet_debug(uint8_t a, uint8_t b, const char *s)
{
    Packet pkt;
    pkt.type = (uint8_t)PacketType::Debug;
    pkt.size = MIN(MAX_PKT_PAYLOAD, strlen(s) + 2);
    pkt.payload[0] = a;
    pkt.payload[1] = b;
    strncpy((char *)&pkt.payload[2], s, pkt.size - 2);
    write(1, &pkt, pkt.size + 2);
}

uint8_t incoming_buffer[sizeof(Packet)];
uint8_t incoming_count;

Packet *check_incoming()
{
    while (incoming_count < 2)
    {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) return nullptr;
        incoming_buffer[incoming_count] = (uint8_t)ch;
        incoming_count++;
    }

    uint8_t size = incoming_buffer[1];
    if (size > MAX_PKT_PAYLOAD)
    {
        incoming_count = 0;
        return nullptr;
    }

    while (incoming_count < (size + 2))
    {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) return nullptr;
        incoming_buffer[incoming_count] = (uint8_t)ch;
        incoming_count++;
    }

    incoming_count = 0;

    return (Packet *)incoming_buffer;
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

    
    for( uint i = 0; i < MAX_ROM_SIZE; i++ )
    {
        rom_data[i] = flash_rom_data[i];
    }

    rom_size = config->rom_size;

    uint sm_data = pio_claim_unused_sm(pio0, true);
    uint sm_oe = pio_claim_unused_sm(pio0, true);
    init_data_bus_programs(pio0, sm_data, sm_oe);

    multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));

    while (true)
    {
        // Reset state
        incoming_count = 0;
        rom_offset = 0;

        // Wait for connection
        while(!tud_cdc_connected())
        {
            sleep_ms(1);
        }

        // Flush input
        while( getchar_timeout_us(0) != PICO_ERROR_TIMEOUT ) {};

        // Write preamble
        write(1, "PicoROM Hello", 13);

        // Loop while connected
        while (tud_cdc_connected())
        {
            const Packet *req = check_incoming();
            if (req)
            {
                switch((PacketType)req->type)
                {
                    case PacketType::IdentReq:
                    {
                        send_packet_string(PacketType::IdentResp, config->name);
                        //send_packet_debug(req->type, req->size, "PKT");
                        break;
                    }

                    case PacketType::IdentSet:
                    {
                        save_config((const char *)req->payload, req->size, config->rom_size);
                        break;
                    }

                    case PacketType::SetPointer:
                    {
                        memcpy(&rom_offset, req->payload, sizeof(uint32_t));
                        //send_packet(PacketType::Debug, &rom_offset, 4);
                        break;
                    }

                    case PacketType::GetPointer:
                    {
                        send_packet(PacketType::CurPointer, &rom_offset, sizeof(rom_offset));
                        break;
                    }

                    case PacketType::Write:
                    {
                        uint32_t mask = rom_size - 1;
                        uint32_t offset = rom_offset & mask;
                        if ((offset + req->size) > rom_size) break; // TODO error reporting
                        memcpy(rom_data + offset, req->payload, req->size);
                        rom_offset += req->size;
                        break;
                    }

                    case PacketType::Read:
                    {
                        uint32_t mask = rom_size - 1;
                        uint32_t offset = rom_offset & mask;
                        uint32_t size = MIN(MAX_PKT_PAYLOAD, rom_size - offset);
                        send_packet(PacketType::ReadData, rom_data + offset, size);
                        rom_offset += size;
                        break;
                    }

                    case PacketType::SetSize:
                    {
                        memcpy(&rom_size, req->payload, sizeof(uint32_t));

                        // restart with new size
                        multicore_reset_core1();
                        multicore_launch_core1_with_stack(core1_entry, core1_stack, sizeof(core1_stack));
                        break;
                    }

                    case PacketType::GetSize:
                    {
                        send_packet(PacketType::CurSize, &rom_size, sizeof(rom_size));
                        break;
                    }

                    case PacketType::CommitFlash:
                    {
                        save_rom();
                        break;
                    }

                    default:
                    {
                        //send_packet_string(PacketType::Debug, "hello");
                        break;
                    }
                }
            }
        }
    }
}

#include <stdio.h>
#include <unistd.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/flash.h"
#include "hardware/structs/syscfg.h"
#include "hardware/structs/bus_ctrl.h"
#include "pico/binary_info.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"

#include <tusb.h>

#include "system.h"
#include "pico_link.h"
#include "state_machines.h"


static constexpr uint FLASH_ROM_OFFSET = FLASH_SIZE - ROM_SIZE;
static constexpr uint FLASH_CFG_OFFSET = FLASH_ROM_OFFSET - FLASH_SECTOR_SIZE;

static constexpr uint CONFIG_VERSION = 0x00010007;

uint32_t rom_offset = 0;
uint8_t *rom_data = (uint8_t *)0x21000000; // Start of 4 64kb sram banks
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


uint32_t core1_stack[8];
void __attribute__((noreturn, section(".time_critical.core1_rom_loop"))) core1_rom_loop()
{
    register uint32_t r0 __asm__("r0") = (uint32_t)rom_data;
    register uint32_t r1 __asm__("r1") = ADDR_MASK;
    register uint32_t r2 __asm__("r2") = (uint32_t)&pio0->txf[0];

    __asm__ volatile (
        "ldr r5, =0xd0000004 \n\t"
        "loop: \n\t"
        "ldr r3, [r5] \n\t"
        "and r3, r1 \n\t"
        "ldrb r3, [r0, r3] \n\t"
        "strb r3, [r2] \n\t"
        "b loop \n\t"
        : "+r" (r0), "+r" (r1), "+r" (r2)
        :
        : "r5", "cc", "memory"
    );

    __builtin_unreachable();
}


void save_config()
{
    if( !memcmp(&config, flash_config, sizeof(Config))) return;

    multicore_reset_core1();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CFG_OFFSET, (uint8_t *)&config, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    multicore_launch_core1_with_stack(core1_rom_loop, core1_stack, sizeof(core1_stack));
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
    multicore_reset_core1();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_ROM_OFFSET, ROM_SIZE);
    flash_range_program(FLASH_ROM_OFFSET, rom_data, ROM_SIZE);
    restore_interrupts(ints);
    multicore_launch_core1_with_stack(core1_rom_loop, core1_stack, sizeof(core1_stack));
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

struct CommsRegisters
{
    uint8_t magic[4];
    uint8_t active;
    uint8_t in_byte;
    uint8_t in_seq;
    uint8_t out_seq;
    uint8_t pending;

    uint8_t pio00;
    uint8_t pio01;
    uint8_t pio10;
    uint8_t pio11;

    uint8_t reserved[256 - 13];

    uint8_t out_area[256];
};

static_assert(sizeof(CommsRegisters) == 512);

static CommsRegisters *comms_reg = (CommsRegisters *)0x0;
static uint32_t comms_reg_addr = 0;

void start_comms(uint32_t addr)
{
    comms_reg_addr = addr & ADDR_MASK & ~0x1ff;
    comms_reg = (CommsRegisters *)(rom_data + comms_reg_addr);
    memset(comms_reg, 0, sizeof(CommsRegisters));
    memcpy(comms_reg->magic, "PICO", 4);
    
    start_comms_programs(comms_reg_addr, offsetof(CommsRegisters, in_byte));

    comms_reg->active = 1;
}

void end_comms()
{
    if (comms_reg == nullptr) return;

    comms_reg->active = 0;
    comms_reg = nullptr;

    end_comms_programs();
}

static void update_comms_out(uint8_t *outbytes, int *outcount, int max_outcount)
{
    while( comms_poll_write(outbytes + *outcount) )
    {
        (*outcount)++;
        comms_reg->out_seq++;

        if (*outcount == max_outcount)
        {
            pl_send_payload(PacketType::CommsData, outbytes, *outcount);
            *outcount = 0;
        }
    }
}

void update_comms(const uint8_t *data, uint32_t len)
{
    if (comms_reg == nullptr) return;

    uint8_t outbytes[MAX_PKT_PAYLOAD];
    int outcount = 0;

    comms_reg->pio00 = pio0->sm[0].addr;
    comms_reg->pio01 = pio0->sm[1].addr;
    comms_reg->pio10 = pio1->sm[0].addr;
    comms_reg->pio11 = pio1->sm[1].addr;

    update_comms_out(outbytes, &outcount, sizeof(outbytes));

    uint incount = 0;

    while (incount < len)
    {
        comms_reg->pending = 1;
        comms_reg->in_byte = data[incount];
        comms_reg->in_seq++;
        incount++;

        while( !comms_poll_read() )
        {
            update_comms_out(outbytes, &outcount, sizeof(outbytes));
        }
    }

    comms_reg->pending = 0;
    
    if (outcount > 0)
    {
        pl_send_payload(PacketType::CommsData, outbytes, outcount);
    }
}

int main()
{
    stdio_init_all();

    init_config();

    set_sys_clock_khz(200000, true);

    configure_address_pins(config.addr_mask);

    for( uint i = 0; i < ROM_SIZE; i++ )
    {
        rom_data[i] = flash_rom_data[i];
    }

    init_data_bus_programs();

    // give core1 bus priority
    bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;

    multicore_reset_core1();
    multicore_launch_core1_with_stack(core1_rom_loop, core1_stack, sizeof(core1_stack));

    while (true)
    {
        // Reset state
        rom_offset = 0;
        end_comms();

        pl_wait_for_connection();

        pl_send_debug("Connected", 1, 2);

        // Loop while connected
        while (pl_is_connected())
        {
            uint32_t addr = sio_hw->gpio_in & config.addr_mask;
            update_comms(nullptr, 0);

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
                        pl_send_null(PacketType::CommitDone);
                        break;
                    }

                    case PacketType::CommsStart:
                    {
                        uint32_t addr;
                        memcpy(&addr, req->payload, 4);
                        start_comms(addr);
                        pl_send_debug("Comms Started", addr, 0);
                        break;
                    }

                    case PacketType::CommsEnd:
                    {
                        end_comms();
                        pl_send_debug("Comms Ended", 0, 0);
                        break;
                    }

                    case PacketType::CommsData:
                    {
                        update_comms(req->payload, req->size);
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

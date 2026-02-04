#if !defined(FLASH_H)
#define FLASH_H 1

#include <stddef.h>
#include <stdint.h>

#include "peripherals.h"

#include "flash_name.h"

struct Config
{
    uint32_t version;
    char name[CONFIG_NAME_LEN];
    char rom_name[CONFIG_ROM_NAME_LEN];
    ResetLevel initial_reset;
    ResetLevel default_reset;
    uint32_t addr_mask;
};

void flash_save_config(const Config *config);
void flash_init_config(Config *config);
void flash_save_rom();
uint32_t flash_load_rom();

#endif // FLASH_H

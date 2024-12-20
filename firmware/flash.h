#if !defined( FLASH_H )
#define FLASH_H 1

#include <stdint.h>
#include <stddef.h>

enum class ResetLevel : uint8_t
{
    Low,
    High,
    Z
};

struct Config
{
    uint32_t version;
    char name[16];
    char rom_name[16];
    ResetLevel initial_reset;
    ResetLevel default_reset;
    uint32_t addr_mask;
};

void flash_save_config(const Config *config);
void flash_init_config(Config *config);
void flash_save_rom();
uint32_t flash_load_rom();

#endif // FLASH_H

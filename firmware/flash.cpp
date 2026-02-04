#include <cstdlib>
#include <stdarg.h>
#include <string.h>
#include <strings.h>

#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/structs/ssi.h"

#include "pico/sync.h"
#include "pico/unique_id.h"

#include "flash.h"
#include "rom.h"
#include "system.h"

static constexpr uint FLASH_CFG_OFFSET = FLASH_SIZE - FLASH_SECTOR_SIZE;
static constexpr uint FLASH_ROM_OFFSET = FLASH_CFG_OFFSET - ROM_SIZE;

static constexpr uint CONFIG_VERSION = 0x00010009;

const uint8_t *flash_rom_data = (uint8_t *)(XIP_BASE + FLASH_ROM_OFFSET);
const Config *flash_config = (Config *)(XIP_BASE + FLASH_CFG_OFFSET);
static_assert(sizeof(Config) <= FLASH_PAGE_SIZE);


void __no_inline_not_in_flash_func(flash_bulk_read)(uint32_t *rxbuf, uint32_t flash_offs, size_t len,
                                                    uint dma_chan)
{
    // SSI must be disabled to set transfer size. If software is executing
    // from flash right now then it's about to have a bad time
    ssi_hw->ssienr = 0;
    ssi_hw->ctrlr1 = len - 1; // NDF, number of data frames
    ssi_hw->dmacr = SSI_DMACR_TDMAE_BITS | SSI_DMACR_RDMAE_BITS;
    ssi_hw->ssienr = 1;
    // Other than NDF, the SSI configuration used for XIP is suitable for a bulk read too.

    // Configure and start the DMA. Note we are avoiding the dma_*() functions
    // as we can't guarantee they'll be inlined
    dma_hw->ch[dma_chan].read_addr = (uint32_t)&ssi_hw->dr0;
    dma_hw->ch[dma_chan].write_addr = (uint32_t)rxbuf;
    dma_hw->ch[dma_chan].transfer_count = len;
    // Must enable DMA byteswap because non-XIP 32-bit flash transfers are
    // big-endian on SSI (we added a hardware tweak to make XIP sensible)
    dma_hw->ch[dma_chan].ctrl_trig =
          DMA_CH0_CTRL_TRIG_BSWAP_BITS |
          DREQ_XIP_SSIRX << DMA_CH0_CTRL_TRIG_TREQ_SEL_LSB |
          dma_chan << DMA_CH0_CTRL_TRIG_CHAIN_TO_LSB |
          DMA_CH0_CTRL_TRIG_INCR_WRITE_BITS |
          DMA_CH0_CTRL_TRIG_DATA_SIZE_VALUE_SIZE_WORD << DMA_CH0_CTRL_TRIG_DATA_SIZE_LSB |
          DMA_CH0_CTRL_TRIG_EN_BITS;

    // Now DMA is waiting, kick off the SSI transfer (mode continuation bits in LSBs)
    ssi_hw->dr0 = (flash_offs << 8u) | 0xa0u;

    // Wait for DMA finish
    while (dma_hw->ch[dma_chan].ctrl_trig & DMA_CH0_CTRL_TRIG_BUSY_BITS);

    // Reconfigure SSI before we jump back into flash!
    ssi_hw->ssienr = 0;
    ssi_hw->ctrlr1 = 0; // Single 32-bit data frame per transfer
    ssi_hw->dmacr = 0;
    ssi_hw->ssienr = 1;
}

void flash_save_config(const Config *config)
{
    if (!memcmp(config, flash_config, sizeof(Config))) return;

    rom_service_stop();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_CFG_OFFSET, (const uint8_t *)config, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    rom_service_start();
}


void flash_init_config(Config *config)
{
    memcpy(config, flash_config, sizeof(Config));

    if (config->version == CONFIG_VERSION) return;

    memset(config, 0, sizeof(Config));

    config->addr_mask = ADDR_MASK;
    config->version = CONFIG_VERSION;
    config->default_reset = ResetLevel::Z;
    config->initial_reset = ResetLevel::Z;
    pico_get_unique_board_id_string(config->name, sizeof(config->name));

    flash_save_config(config);
}


void flash_save_rom()
{
    rom_service_stop();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_ROM_OFFSET, ROM_SIZE);
    flash_range_program(FLASH_ROM_OFFSET, rom_get_buffer(), ROM_SIZE);
    restore_interrupts(ints);
    rom_service_start();
}

uint32_t flash_load_rom()
{
    uint32_t start_time = time_us_32();

    uint32_t ints = save_and_disable_interrupts();
    flash_bulk_read((uint32_t *)rom_get_buffer(), FLASH_ROM_OFFSET, ROM_SIZE / 4, DMA_CH_FLASH);
    restore_interrupts(ints);

    //memcpy(rom_get_buffer(), flash_rom_data, ROM_SIZE);

    uint32_t flash_load_time = time_us_32() - start_time;

    return flash_load_time;
}

extern "C" const char* flash_get_device_name(void)
{
    return flash_config->name;
}

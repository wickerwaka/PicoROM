#include <assert.h>
#include <string.h>

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>

#include "flash_utils.h"
#include "linker_definitions.h"

static inline void erase_flash_info_partition_isr_unsafe(void) {
    flash_range_erase(PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_INFO_START),
                      FLASH_SECTOR_SIZE);
}

static void
overwrite_4_bytes_in_flash_isr_unsafe(uint32_t dest_addr_with_xip_offset,
                                      uint32_t data) {
    uint8_t data_arr_u8[FLASH_SECTOR_SIZE];
    uint32_t *data_ptr_u32 = (uint32_t *) data_arr_u8;
    uint32_t erase_start_addr_with_xip_offset =
            PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_INFO_START);

    assert(dest_addr_with_xip_offset >= erase_start_addr_with_xip_offset);

    void *flash_info_start_addr =
            (void *) (PFB_ADDR_AS_U32(__FLASH_INFO_START));
    memcpy(data_arr_u8, flash_info_start_addr, FLASH_SECTOR_SIZE);

    size_t array_index =
            (dest_addr_with_xip_offset - erase_start_addr_with_xip_offset)
            / (sizeof(uint32_t));
    data_ptr_u32[array_index] = data;

    erase_flash_info_partition_isr_unsafe();
    flash_range_program(erase_start_addr_with_xip_offset, data_arr_u8,
                        FLASH_SECTOR_SIZE);
}

static void overwrite_4_bytes_in_flash(uint32_t dest_addr, uint32_t data) {
    uint32_t saved_interrupts = save_and_disable_interrupts();
    overwrite_4_bytes_in_flash_isr_unsafe(dest_addr - XIP_BASE, data);
    restore_interrupts(saved_interrupts);
}

void flash_utils_mark_download_slot(uint32_t magic) {
    uint32_t dest_addr = PFB_ADDR_AS_U32(__FLASH_INFO_IS_DOWNLOAD_SLOT_VALID);

    overwrite_4_bytes_in_flash(dest_addr, magic);
}

void flash_utils_notify_pico_about_firmware(uint32_t magic) {
    uint32_t dest_addr = PFB_ADDR_AS_U32(__FLASH_INFO_IS_FIRMWARE_SWAPPED);

    overwrite_4_bytes_in_flash(dest_addr, magic);
}

void flash_utils_mark_if_should_rollback(uint32_t magic) {
    uint32_t dest_addr = PFB_ADDR_AS_U32(__FLASH_INFO_SHOULD_ROLLBACK);

    overwrite_4_bytes_in_flash(dest_addr, magic);
}

void flash_utils_mark_if_is_after_rollback(uint32_t magic) {
    uint32_t dest_addr = PFB_ADDR_AS_U32(__FLASH_INFO_IS_AFTER_ROLLBACK);

    overwrite_4_bytes_in_flash(dest_addr, magic);
}

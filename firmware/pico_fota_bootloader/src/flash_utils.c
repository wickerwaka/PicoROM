#include <assert.h>
#include <string.h>

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/stdlib.h>

#include "flash_utils.h"
#include "linker_definitions.h"

#define FLASH_INFO_COUNT 8
uint32_t flash_info_work[FLASH_INFO_COUNT];

static inline void erase_flash_info_partition_isr_unsafe(void) {
    flash_range_erase(PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_INFO_START),
                      FLASH_SECTOR_SIZE);
}

static void memcpy_nowarn(void *d, const void *s, size_t len)
{
    asm("" : "+r"(d), "+r"(s));
    memcpy(d, s, len);
}

static void overwrite_4_bytes_in_flash_isr_unsafe(uint32_t *dest_addr,
                                      uint32_t data) {
    memcpy_nowarn(flash_info_work, &__FLASH_INFO_START, sizeof(flash_info_work));

    size_t array_index = dest_addr - &__FLASH_INFO_START;
    if (array_index >= FLASH_INFO_COUNT) return;

    flash_info_work[array_index] = data;

    erase_flash_info_partition_isr_unsafe();
    flash_range_program(PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_INFO_START),
                        (const uint8_t *)flash_info_work,
                        FLASH_PAGE_SIZE);
}

static void overwrite_4_bytes_in_flash(uint32_t *dest_addr, uint32_t data) {
    if (*dest_addr == data) return;

    uint32_t saved_interrupts = save_and_disable_interrupts();
    overwrite_4_bytes_in_flash_isr_unsafe(dest_addr, data);
    restore_interrupts(saved_interrupts);
}

void flash_utils_mark_download_slot(uint32_t magic) {
    overwrite_4_bytes_in_flash(&__FLASH_INFO_IS_DOWNLOAD_SLOT_VALID, magic);
}

void flash_utils_notify_pico_about_firmware(uint32_t magic) {
    overwrite_4_bytes_in_flash(&__FLASH_INFO_IS_FIRMWARE_SWAPPED, magic);
}

void flash_utils_mark_if_should_rollback(uint32_t magic) {
    overwrite_4_bytes_in_flash(&__FLASH_INFO_SHOULD_ROLLBACK, magic);
}

void flash_utils_mark_if_is_after_rollback(uint32_t magic) {
    overwrite_4_bytes_in_flash(&__FLASH_INFO_IS_AFTER_ROLLBACK, magic);
}

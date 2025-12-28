#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <pico/stdlib.h>

/**
 * Some random values tbh.
 */
#define PFB_SHOULD_SWAP_MAGIC 0xabcdef12
#define PFB_SHOULD_NOT_SWAP_MAGIC 0x00000000

#define PFB_HAS_NEW_FIRMWARE_MAGIC 0x12345678
#define PFB_NO_NEW_FIRMWARE_MAGIC 0x00000000

#define PFB_IS_AFTER_ROLLBACK_MAGIC 0xbeefbeef
#define PFB_IS_NOT_AFTER_ROLLBACK_MAGIC 0x00000000

#define PFB_SHOULD_ROLLBACK_MAGIC 0xdeadead
#define PFB_SHOULD_NOT_ROLLBACK_MAGIC 0x00000000

#define PFB_ADDR_AS_U32(Data) ((uint32_t) & (Data))
#define PFB_ADDR_WITH_XIP_OFFSET_AS_U32(Data) \
    (PFB_ADDR_AS_U32(Data) - (XIP_BASE))

void flash_utils_mark_download_slot(uint32_t magic);
void flash_utils_notify_pico_about_firmware(uint32_t magic);
void flash_utils_mark_if_should_rollback(uint32_t magic);
void flash_utils_mark_if_is_after_rollback(uint32_t magic);

#ifdef __cplusplus
}
#endif

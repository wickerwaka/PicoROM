/*
 * Copyright (c) 2024 Jakub Zimnol
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>

#ifdef PFB_WITH_IMAGE_ENCRYPTION
#    include <mbedtls/aes.h>
#endif // PFB_WITH_IMAGE_ENCRYPTION
#ifdef PFB_WITH_SHA256_HASHING
#    include <mbedtls/sha256.h>
#endif // PFB_WITH_SHA256_HASHING

#include <pico_fota_bootloader/core.h>

#include "flash_utils.h"
#include "linker_definitions.h"

#define PFB_SHA256_DIGEST_SIZE 32
#define PFB_CRC32_DIGEST_SIZE 4
#define PFB_AES_BLOCK_SIZE 16

#ifdef PFB_WITH_IMAGE_ENCRYPTION
mbedtls_aes_context m_aes_ctx;
#endif // PFB_WITH_IMAGE_ENCRYPTION

static void *get_image_sha256_address(size_t image_size) {
    return (void *) (PFB_ADDR_AS_U32(__FLASH_DOWNLOAD_SLOT_START) + image_size
                     - PFB_SHA256_DIGEST_SIZE);
}

static void *get_image_crc32_address(size_t image_size) {
    return (void *) (PFB_ADDR_AS_U32(__FLASH_DOWNLOAD_SLOT_START) + image_size
                     - PFB_CRC32_DIGEST_SIZE);
}


#ifdef PFB_WITH_IMAGE_ENCRYPTION
static int decrypt_256_bytes(const uint8_t *src, uint8_t *out_dest) {
    for (int i = 0; i < PFB_ALIGN_SIZE / PFB_AES_BLOCK_SIZE; i++) {
        int ret = mbedtls_aes_crypt_ecb(&m_aes_ctx, MBEDTLS_AES_DECRYPT,
                                        src + i * PFB_AES_BLOCK_SIZE,
                                        out_dest + i * PFB_AES_BLOCK_SIZE);
        if (ret) {
            return ret;
        }
    }
    return 0;
}
#endif // PFB_WITH_IMAGE_ENCRYPTION

void pfb_mark_download_slot_as_valid(void) {
    flash_utils_mark_download_slot(PFB_SHOULD_SWAP_MAGIC);
}

void pfb_mark_download_slot_as_invalid(void) {
    flash_utils_mark_download_slot(PFB_SHOULD_NOT_SWAP_MAGIC);
}

bool pfb_is_after_firmware_update(void) {
    return (__FLASH_INFO_IS_FIRMWARE_SWAPPED == PFB_HAS_NEW_FIRMWARE_MAGIC);
}

int pfb_write_to_flash_aligned_256_bytes(uint8_t *src,
                                         size_t offset_bytes,
                                         size_t len_bytes) {
    if (len_bytes % PFB_ALIGN_SIZE || offset_bytes % PFB_ALIGN_SIZE
        || offset_bytes + len_bytes
                   > (size_t) PFB_ADDR_AS_U32(__FLASH_SWAP_SPACE_LENGTH)) {
        return 1;
    }

    for (size_t i = 0; i < len_bytes / PFB_ALIGN_SIZE; i++) {
#ifdef PFB_WITH_IMAGE_ENCRYPTION
        unsigned char output_aes_dec[PFB_ALIGN_SIZE];
        int ret = decrypt_256_bytes(src + i * PFB_ALIGN_SIZE, output_aes_dec);
        if (ret) {
            return ret;
        }
#endif // PFB_WITH_IMAGE_ENCRYPTION
        uint32_t dest_address =
                PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_DOWNLOAD_SLOT_START)
                + offset_bytes + i * PFB_ALIGN_SIZE;
        uint8_t *src_address =
#ifdef PFB_WITH_IMAGE_ENCRYPTION
                output_aes_dec;
#else  // PFB_WITH_IMAGE_ENCRYPTION
                src + i * PFB_ALIGN_SIZE;
#endif // PFB_WITH_IMAGE_ENCRYPTION
        uint32_t saved_interrupts = save_and_disable_interrupts();
        flash_range_program(dest_address, src_address, PFB_ALIGN_SIZE);
        restore_interrupts(saved_interrupts);
    }
    return 0;
}

int pfb_initialize_download_slot(void) {
    uint32_t erase_len = PFB_ADDR_AS_U32(__FLASH_SWAP_SPACE_LENGTH);
    uint32_t erase_address_with_xip_offset =
            PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_DOWNLOAD_SLOT_START);
    assert(erase_len % FLASH_SECTOR_SIZE == 0);

    pfb_firmware_commit();

    uint32_t saved_interrupts = save_and_disable_interrupts();
    flash_range_erase(erase_address_with_xip_offset, erase_len);
    restore_interrupts(saved_interrupts);

#ifdef PFB_WITH_IMAGE_ENCRYPTION
    mbedtls_aes_free(&m_aes_ctx);
    mbedtls_aes_init(&m_aes_ctx);
    int ret = mbedtls_aes_setkey_dec(&m_aes_ctx,
                                     (const unsigned char *) PFB_AES_KEY,
                                     strlen((const char *) PFB_AES_KEY) * 8);
    if (ret) {
        return ret;
    }
#endif // PFB_WITH_IMAGE_ENCRYPTION

    return 0;
}

void pfb_perform_update(void) {
#ifdef PFB_WITH_IMAGE_ENCRYPTION
    mbedtls_aes_free(&m_aes_ctx);
#endif // PFB_WITH_IMAGE_ENCRYPTION
    watchdog_enable(1, 1);
    while (1)
        ;
}

void pfb_firmware_commit(void) {
    flash_utils_mark_if_should_rollback(PFB_SHOULD_NOT_ROLLBACK_MAGIC);
}

bool pfb_is_after_rollback(void) {
    return (__FLASH_INFO_IS_AFTER_ROLLBACK == PFB_IS_AFTER_ROLLBACK_MAGIC);
}

#ifdef PFB_WITH_SHA256_HASHING
#if MBEDTLS_VERSION_MAJOR >= 3
#define mbedtls_sha256_starts_ret(ctx, is224)  mbedtls_sha256_starts(ctx, is224)
#define mbedtls_sha256_update_ret(ctx, input, ilen)  mbedtls_sha256_update(ctx, input, ilen)
#define mbedtls_sha256_finish_ret(ctx, output)  mbedtls_sha256_finish(ctx, output)
#endif // MBEDTLS_VERSION_MAJOR
#endif // PFB_WITH_SHA256_HASHING

#ifdef PFB_WITH_CRC32_HASHING
static const uint32_t crc_table[256] = {
	0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
	0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
	0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
	0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
	0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
	0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
	0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
	0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
	0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
	0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
	0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
	0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
	0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
	0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
	0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
	0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
	0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
	0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
	0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
	0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
	0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
	0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
	0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
	0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
	0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
	0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
	0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
	0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
	0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
	0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
	0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
	0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
	0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
	0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
	0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
	0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
	0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
	0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
	0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
	0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
	0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
	0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
	0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
	0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
	0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
	0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
	0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
	0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
	0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
	0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
	0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
	0x2d02ef8dL
};

#define DO1(buf) crc = crc_table[((int)crc ^ (*buf++)) & 0xff] ^ (crc >> 8);
#define DO2(buf)  DO1(buf); DO1(buf);
#define DO4(buf)  DO2(buf); DO2(buf);
#define DO8(buf)  DO4(buf); DO4(buf);

static uint32_t calc_crc32(const unsigned char *buffer, unsigned int len)
{
	uint32_t crc;
	crc = 0;
	crc = crc ^ 0xffffffffL;
	while(len >= 8) {
		DO8(buffer);
		len -= 8;
	}
	if(len) do {
		DO1(buffer);
	} while(--len);
	return crc ^ 0xffffffffL;
}
#endif // PFB_WITH_CRC32_HASHING

int pfb_firmware_hash_check(size_t firmware_size) {
#ifdef PFB_WITH_SHA256_HASHING
    if (firmware_size % PFB_ALIGN_SIZE || firmware_size < PFB_ALIGN_SIZE) {
        return 1;
    }

    mbedtls_sha256_context sha256_ctx;
    mbedtls_sha256_init(&sha256_ctx);

    int ret;
    ret = mbedtls_sha256_starts_ret(&sha256_ctx, 0);
    if (ret) {
        return ret;
    }

    uint32_t image_start_address = PFB_ADDR_AS_U32(__FLASH_DOWNLOAD_SLOT_START);
    size_t image_size_without_sha256 = firmware_size - 256;
    ret = mbedtls_sha256_update_ret(&sha256_ctx,
                                    (const unsigned char *) image_start_address,
                                    image_size_without_sha256);
    if (ret) {
        return ret;
    }

    unsigned char calculated_sha256[PFB_SHA256_DIGEST_SIZE];
    ret = mbedtls_sha256_finish_ret(&sha256_ctx, calculated_sha256);
    if (ret) {
        return ret;
    }

    mbedtls_sha256_free(&sha256_ctx);

    void *image_sha256_address = get_image_sha256_address(firmware_size);
    if (memcmp(calculated_sha256, image_sha256_address, PFB_SHA256_DIGEST_SIZE)
        != 0) {
        return 1;
    }
#elif defined(PFB_WITH_CRC32_HASHING)
    if (firmware_size % PFB_ALIGN_SIZE || firmware_size < PFB_ALIGN_SIZE) {
        return 1;
    }

    uint32_t image_start_address = PFB_ADDR_AS_U32(__FLASH_DOWNLOAD_SLOT_START);
    size_t image_size_without_crc32 = firmware_size - 256;
    uint32_t calculated_crc32 = calc_crc32((const unsigned char *)image_start_address, image_size_without_crc32);

    void *image_crc32_address = get_image_crc32_address(firmware_size);
    if (memcmp(&calculated_crc32, image_crc32_address, PFB_CRC32_DIGEST_SIZE)
        != 0) {
        return 1;
    }
#endif // PFB_WITH_SHA256_HASHING
    (void) firmware_size;

    return 0;
}

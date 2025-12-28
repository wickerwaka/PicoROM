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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <pico/stdlib.h>

#define PFB_ALIGN_SIZE (256)

/**
 * Marks the download slot as valid, i.e. download slot contains proper binary
 * content and the partitions can be swapped. MUST be called before the next
 * reboot, otherwise data from the download slot will be lost.
 */
void pfb_mark_download_slot_as_valid(void);

/**
 * Marks the download slot as invalid, i.e. download slot no longer contains
 * proper binary content and the partitions MUST NOT be swapped.
 */
void pfb_mark_download_slot_as_invalid(void);

/**
 * Returns the information if the executed app comes from the download slot.
 * NOTE: this function will return true only if the firmware update has been
 *       executed during the previous reboot.
 *
 * @return true if the partitions were swapped during the previous reboot,
 *         false otherwise.
 */
bool pfb_is_after_firmware_update(void);

/**
 * Writes data into the download partition and checks, if the length of the data
 * is 256 bytes alligned.
 * If @ref PFB_WITH_IMAGE_ENCRYPTION is defined, the function will decrypt the
 * downloaded data using the PFB_AES_KEY.
 *
 * @param src          Pointer to the source buffer.
 * @param offset_bytes Offset which should be applied to the beginning of the
 *                     download slot partition.
 * @param len_bytes    Number of bytes that should be written into flash. MUST
 *                     be a multiple of 256.
 *
 * @return 1 when @p len_bytes or @p offset_bytes are not multiple of 256 or
 *         when ( @p offset_bytes + @p len_bytes ) exceeds download slot size,
 *         negative mbedtls error code in case of an error if
 *         @ref PFB_WITH_IMAGE_ENCRYPTION is defined,
 *         0 otherwise.
 */
int pfb_write_to_flash_aligned_256_bytes(uint8_t *src,
                                         size_t offset_bytes,
                                         size_t len_bytes);

/**
 * Initializes the download slot, i.e. erases the download partition. MUST be
 * called before writing data into the flash. Before an erase, the function will
 * call @ref pfb_firmware_commit even if @ref pfb_firmware_commit has been
 * called before.
 *
 * @return mbedtls error code in case of a mbedtls error if
 *         @ref PFB_WITH_IMAGE_ENCRYPTION is defined,
 *         0 otherwise.
 */
int pfb_initialize_download_slot(void);

/**
 * Performs the firmware update. Reboots the Pico and checks if the partitions
 * should be swapped.
 */
void pfb_perform_update(void);

/**
 * Marks the information that the device SHOULD NOT perform rollback in case of
 * a reboot.
 */
void pfb_firmware_commit(void);

/**
 * Returns the information if the device has performed a rollback during the
 * reboot.
 * NOTE: this function will return true only if the rollback has been performed
 *       during the very previous reboot.
 *
 * @return true if the rollback has been performed during the previous reboot,
 *         false otherwise.
 */
bool pfb_is_after_rollback(void);

/**
 * If @ref WITH_SHA256 is defined, checks if the calculated SHA256 of the image
 * matches the expected one. Otherwise, the function will only return 0.
 *
 * @param firmware_size Size of the downloaded firmware image in bytes.
 *
 * @return A negative mbedtls error code on calculation error,
 *         1 if the firmware size is not a multiple of 256 or if the calculated
 *         and expected sha are different,
 *         0 otherwise.
 *         If @ref PFB_WITH_SHA256_HASHING is not defined, the function will
 *         always return 0.
 */
int pfb_firmware_sha256_check(size_t firmware_size);

#ifdef __cplusplus
}
#endif

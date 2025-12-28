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

#include <pico/stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t __flash_info_app_vtor;
extern uint32_t __FLASH_START;
extern uint32_t __FLASH_INFO_START;
extern uint32_t __FLASH_INFO_APP_HEADER;
extern uint32_t __FLASH_INFO_DOWNLOAD_HEADER;
extern uint32_t __FLASH_INFO_IS_DOWNLOAD_SLOT_VALID;
extern uint32_t __FLASH_INFO_IS_FIRMWARE_SWAPPED;
extern uint32_t __FLASH_INFO_IS_AFTER_ROLLBACK;
extern uint32_t __FLASH_INFO_SHOULD_ROLLBACK;
extern uint32_t __FLASH_APP_START;
extern uint32_t __FLASH_DOWNLOAD_SLOT_START;
extern uint32_t __FLASH_SWAP_SPACE_LENGTH;

#ifdef __cplusplus
}
#endif

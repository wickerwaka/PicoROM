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

#include <stdio.h>
#include <string.h>

#if defined(PICO_RP2350)
#include <RP2350.h>
#elif defined(PICO_RP2040)
#include <RP2040.h>
#else
#error "Unsupported PICO platform"
#endif

#include <hardware/flash.h>
#include <hardware/resets.h>
#include <hardware/sync.h>
#include <pico/bootrom.h>
#include <pico/stdlib.h>

#include <pico_fota_bootloader/core.h>

#include "flash_utils.h"
#include "linker_definitions.h"

#ifdef PFB_WITH_BOOTLOADER_LOGS
#    define BOOTLOADER_LOG(...)                \
        do {                                   \
            puts("[BOOTLOADER] " __VA_ARGS__); \
            sleep_ms(5);                       \
        } while (0)
#else // PFB_WITH_BOOTLOADER_LOGS
#    define BOOTLOADER_LOG(...) ((void) 0)
#endif // PFB_WITH_BOOTLOADER_LOGS

static void mark_should_rollback(void) {
    flash_utils_mark_if_should_rollback(PFB_SHOULD_ROLLBACK_MAGIC);
}

static void mark_is_after_rollback(void) {
    flash_utils_mark_if_is_after_rollback(PFB_IS_AFTER_ROLLBACK_MAGIC);
}

static void mark_is_not_after_rollback(void) {
    flash_utils_mark_if_is_after_rollback(PFB_IS_NOT_AFTER_ROLLBACK_MAGIC);
}

static bool should_rollback(void) {
    return (__FLASH_INFO_SHOULD_ROLLBACK == PFB_SHOULD_ROLLBACK_MAGIC);
}

static bool has_firmware_to_swap(void) {
    return (__FLASH_INFO_IS_DOWNLOAD_SLOT_VALID == PFB_SHOULD_SWAP_MAGIC);
}

static void mark_pico_has_new_firmware(void) {
    flash_utils_notify_pico_about_firmware(PFB_HAS_NEW_FIRMWARE_MAGIC);
}

static void mark_has_no_new_firmware(void) {
    flash_utils_notify_pico_about_firmware(PFB_NO_NEW_FIRMWARE_MAGIC);
}

static void swap_images(void) {
    uint8_t swap_buff_from_downlaod_slot[FLASH_SECTOR_SIZE];
    uint8_t swap_buff_from_application_slot[FLASH_SECTOR_SIZE];
    const uint32_t SWAP_ITERATIONS =
            PFB_ADDR_AS_U32(__FLASH_SWAP_SPACE_LENGTH) / FLASH_SECTOR_SIZE;

    uint32_t saved_interrupts = save_and_disable_interrupts();
    for (uint32_t i = 0; i < SWAP_ITERATIONS; i++) {
        memcpy(swap_buff_from_downlaod_slot,
               (void *) (PFB_ADDR_AS_U32(__FLASH_DOWNLOAD_SLOT_START)
                         + i * FLASH_SECTOR_SIZE),
               FLASH_SECTOR_SIZE);
        memcpy(swap_buff_from_application_slot,
               (void *) (PFB_ADDR_AS_U32(__FLASH_APP_START)
                         + i * FLASH_SECTOR_SIZE),
               FLASH_SECTOR_SIZE);
        flash_range_erase(PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_APP_START)
                                  + i * FLASH_SECTOR_SIZE,
                          FLASH_SECTOR_SIZE);
        flash_range_erase(PFB_ADDR_WITH_XIP_OFFSET_AS_U32(
                                  __FLASH_DOWNLOAD_SLOT_START)
                                  + i * FLASH_SECTOR_SIZE,
                          FLASH_SECTOR_SIZE);
        flash_range_program(PFB_ADDR_WITH_XIP_OFFSET_AS_U32(__FLASH_APP_START)
                                    + i * FLASH_SECTOR_SIZE,
                            swap_buff_from_downlaod_slot,
                            FLASH_SECTOR_SIZE);
        flash_range_program(PFB_ADDR_WITH_XIP_OFFSET_AS_U32(
                                    __FLASH_DOWNLOAD_SLOT_START)
                                    + i * FLASH_SECTOR_SIZE,
                            swap_buff_from_application_slot,
                            FLASH_SECTOR_SIZE);
    }
    restore_interrupts(saved_interrupts);
}

static void _disable_interrupts(void) {
    SysTick->CTRL &= ~1;

    NVIC->ICER[0] = 0xFFFFFFFF;
    NVIC->ICPR[0] = 0xFFFFFFFF;
}

static void reset_peripherals(void) {
    reset_block(~(RESETS_RESET_IO_QSPI_BITS | RESETS_RESET_PADS_QSPI_BITS
                  | RESETS_RESET_SYSCFG_BITS | RESETS_RESET_PLL_SYS_BITS));
}

static void jump_to_vtor(uint32_t vtor) {
    // Derived from the Leaf Labs Cortex-M3 bootloader.
    // Copyright (c) 2010 LeafLabs LLC.
    // Modified 2021 Brian Starkey <stark3y@gmail.com>
    // Originally under The MIT License

    uint32_t reset_vector = *(volatile uint32_t *) (vtor + 0x04);
    SCB->VTOR = (volatile uint32_t)(vtor);

    asm volatile("msr msp, %0" ::"g"(*(volatile uint32_t *) vtor));
    asm volatile("bx %0" ::"r"(reset_vector));
}

static bool is_application_slot_empty(void) {
    uint32_t reset_handler =
            *(volatile uint32_t *) (__flash_info_app_vtor + 0x04);
    return (reset_handler < 0x10000000 || reset_handler > 0x10200000);
}

static void print_welcome_message(void) {
#ifdef PFB_WITH_BOOTLOADER_LOGS
    uint32_t space = PFB_ADDR_AS_U32(__FLASH_SWAP_SPACE_LENGTH) / 1024;
    puts("");
    puts("***********************************************************");
    puts("*                                                         *");
    puts("*           Raspberry Pi Pico W FOTA Bootloader           *");
    puts("*             Copyright (c) 2024 Jakub Zimnol             *");
    puts("*                                                         *");
    puts("***********************************************************");
    puts("");
    printf("[BOOTLOADER] Maximum code length: %luK\r\n", space);
#endif // PFB_WITH_BOOTLOADER_LOGS
}

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    print_welcome_message();

    if (should_rollback()) {
        BOOTLOADER_LOG("Rolling back to the previous firmware...");
        swap_images();
        pfb_firmware_commit();
        mark_has_no_new_firmware();
        mark_is_after_rollback();
    } else if (has_firmware_to_swap()) {
        BOOTLOADER_LOG("Swapping images...");
        swap_images();
        mark_pico_has_new_firmware();
        mark_is_not_after_rollback();
        mark_should_rollback();
    } else {
        BOOTLOADER_LOG("Nothing to swap");
        pfb_firmware_commit();
        mark_has_no_new_firmware();
    }

    pfb_mark_download_slot_as_invalid();
    if (is_application_slot_empty()) {
        BOOTLOADER_LOG("Application slot is empty, waiting for application "
                       "binary...");
        sleep_ms(1000);
        reset_usb_boot(0, 0);
    }
    BOOTLOADER_LOG("End of execution, executing the application...\n");

    _disable_interrupts();
    reset_peripherals();
    jump_to_vtor(__flash_info_app_vtor);

    return 0;
}

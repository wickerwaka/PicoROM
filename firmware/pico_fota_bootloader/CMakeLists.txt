# Copyright (c) 2024 Jakub Zimnol
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

cmake_minimum_required(VERSION 3.13)

if(${PICO_PLATFORM} STREQUAL "rp2350-arm-s" OR ${PICO_PLATFORM} STREQUAL "rp2350")
    set(CPU "2350" CACHE STRING "CPU model")
elseif(${PICO_PLATFORM} STREQUAL "rp2040")
    set(CPU "2040" CACHE STRING "CPU model")
else()
    message(FATAL_ERROR "Platform not supported: ${PICO_PLATFORM}")
endif()

########################################
# CMake options
########################################
if ( NOT DEFINED PFB_WITH_BOOTLOADER_LOGS )
    option(PFB_WITH_BOOTLOADER_LOGS "Enables logging messages from the bootloader using stdio" ON)
endif()
if ( NOT DEFINED PFB_REDIRECT_BOOTLOADER_LOGS_TO_UART )
    option(PFB_REDIRECT_BOOTLOADER_LOGS_TO_UART "Redirects bootloader's logs from USB to UART" OFF)
endif()
if ( NOT DEFINED PFB_WITH_IMAGE_ENCRYPTION )
    option(PFB_WITH_IMAGE_ENCRYPTION "Enables image encryption using AES ECB algorithm" ON)
endif()
if ( NOT DEFINED PFB_AES_KEY )
    option(PFB_AES_KEY "AES key used for image encryption and decryption")
endif()
if ( NOT DEFINED PFB_WITH_SHA256_HASHING )
    option(PFB_WITH_SHA256_HASHING "Enables image SHA256 appending and checking" ON)
endif()
if ( NOT PFB_RESERVED_FILESYSTEM_SIZE_KB )
    set(PFB_RESERVED_FILESYSTEM_SIZE_KB 0 CACHE STRING "EXPERIMENTAL: Size in KB reserved for the filesystem")
endif()

########################################
# Build linker definitions
########################################
# PICO_FLASH_SIZE_BYTES
execute_process(
    COMMAND cat ${CMAKE_CURRENT_LIST_DIR}/linker_common/linker_definitions.in
    COMMAND ${CMAKE_C_COMPILER} -DFLASH_SIZE=${PICO_FLASH_SIZE_BYTES}
            -DFILESYSTEM_SIZE=${PFB_RESERVED_FILESYSTEM_SIZE_KB} -E -P
            -o ${CMAKE_BINARY_DIR}/linker_definitions.ld -
    )
########################################
# Check and set AES key
########################################
if (PFB_WITH_IMAGE_ENCRYPTION)
    if (NOT PFB_AES_KEY)
        set(PFB_AES_KEY "default")
        message(WARNING "AES key has been set to: \"${PFB_AES_KEY}\"")
    endif ()
    message(STATUS "AES key: ${PFB_AES_KEY}")
    string(REGEX MATCH "^[0-9a-zA-Z]+" aes_key_match ${PFB_AES_KEY})
    string(LENGTH ${PFB_AES_KEY} aes_key_length)
    if ((NOT ${PFB_AES_KEY} STREQUAL ${aes_key_match}) OR (${aes_key_length} GREATER 32))
        message(FATAL_ERROR
                "AES key must be 32 bytes long and contain only characters from the set [0-9a-zA-Z].")
    endif ()
    math(EXPR num_xs_needed "32 - ${aes_key_length}")
    string(REPEAT "x" ${num_xs_needed} XS)
    string(CONCAT PFB_AES_KEY ${PFB_AES_KEY} ${XS})
    message(STATUS "AES key padded to 32 characters: ${PFB_AES_KEY}")
    set(PFB_AES_KEY_GLOBAL ${PFB_AES_KEY} PARENT_SCOPE)
endif()

################################################################################
# Define the pico_fota_bootloader_lib library
################################################################################
add_library(pico_fota_bootloader_lib STATIC
            src/pico_fota_bootloader.c)
target_sources(pico_fota_bootloader_lib PRIVATE src/flash_utils.c)
target_include_directories(pico_fota_bootloader_lib PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(pico_fota_bootloader_lib PRIVATE
                           linker_common)
target_link_libraries(pico_fota_bootloader_lib PUBLIC
                      hardware_watchdog
                      pico_stdlib
                      hardware_flash)
target_link_libraries(pico_fota_bootloader_lib PRIVATE
                      pico_mbedtls)
target_link_options(pico_fota_bootloader_lib PRIVATE
                    "-T${CMAKE_CURRENT_BINARY_DIR}/linker_definitions.ld")
target_compile_options(pico_fota_bootloader_lib PRIVATE
                       -Wall
                       -Wextra
                       -Wnull-dereference)

if (PFB_WITH_IMAGE_ENCRYPTION)
    target_compile_definitions(pico_fota_bootloader_lib PRIVATE PFB_WITH_IMAGE_ENCRYPTION)
    target_compile_definitions(pico_fota_bootloader_lib PRIVATE PFB_AES_KEY=\"${PFB_AES_KEY}\")
endif ()
if (PFB_WITH_SHA256_HASHING)
    target_compile_definitions(pico_fota_bootloader_lib PRIVATE PFB_WITH_SHA256_HASHING)
endif ()

set(BOOTLOADER_DIR_GLOBAL ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)

########################################
# Manage application binary
########################################
function(pfb_compile_with_bootloader Target)
    target_link_options(${Target} PRIVATE "-L${BOOTLOADER_DIR_GLOBAL}/linker_common" "-L${CMAKE_CURRENT_BINARY_DIR}")
    pico_set_linker_script(${Target} ${BOOTLOADER_DIR_GLOBAL}/linker_common/app${CPU}.ld)

    if (PFB_WITH_SHA256_HASHING OR PFB_WITH_IMAGE_ENCRYPTION)
        find_package(Python COMPONENTS Interpreter REQUIRED)
        if (NOT Python_Interpreter_FOUND)
            message(FATAL_ERROR
                "Python interpreter not found and is required for SHA256 appending and AES image encryption")
        endif ()
    endif ()

    add_custom_command(
        TARGET ${Target}
        POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${Target}>
                                           $<TARGET_PROPERTY:${Target},NAME>_fota_image.bin)

    if (PFB_WITH_SHA256_HASHING)
        add_custom_command(
            TARGET ${Target}
            POST_BUILD
            COMMAND ${Python_EXECUTABLE} ${BOOTLOADER_DIR_GLOBAL}/scripts/sha256_append.py
                --target-file "$<TARGET_PROPERTY:${Target},NAME>_fota_image.bin"
            COMMENT "Appending encrypted FOTA file with SHA256...")
    endif ()
    if (PFB_WITH_IMAGE_ENCRYPTION)
        add_custom_command(
            TARGET ${Target}
            POST_BUILD
            COMMAND ${Python_EXECUTABLE} ${BOOTLOADER_DIR_GLOBAL}/scripts/aes_encrypt.py
                --target-file "$<TARGET_PROPERTY:${Target},NAME>_fota_image.bin"
                --aes-key ${PFB_AES_KEY_GLOBAL}
            COMMENT "Encrypting FOTA image using AES...")
    endif ()
endfunction()

################################################################################
# Create bootloader binary
################################################################################
add_executable(pico_fota_bootloader
               src/bootloader_app.c)
target_sources(pico_fota_bootloader PRIVATE src/flash_utils.c)
target_include_directories(pico_fota_bootloader PRIVATE
                           linker_common)
target_link_libraries(pico_fota_bootloader
                      hardware_structs
                      hardware_resets
                      pico_fota_bootloader_lib
                      cmsis_core)

function (target_compile_link_options Name Option)
    target_compile_options(${Name} PRIVATE ${Option})
    target_link_options(${Name} PRIVATE ${Option})
endfunction ()

target_compile_definitions(pico_fota_bootloader PRIVATE -DPICO_PLATFORM=${PICO_PLATFORM})
target_compile_link_options(pico_fota_bootloader "-Os")
target_compile_link_options(pico_fota_bootloader "-ffunction-sections")
target_compile_link_options(pico_fota_bootloader "-fdata-sections")
target_compile_link_options(pico_fota_bootloader "-L${CMAKE_CURRENT_SOURCE_DIR}/linker_common" "-L${CMAKE_CURRENT_BINARY_DIR}")
target_link_options(pico_fota_bootloader PRIVATE "LINKER:--gc-sections")
target_compile_options(pico_fota_bootloader PRIVATE
                       -Wall
                       -Wextra
                       -Wnull-dereference)

pico_set_linker_script(pico_fota_bootloader ${CMAKE_CURRENT_SOURCE_DIR}/linker_common/boot${CPU}.ld)
pico_add_extra_outputs(pico_fota_bootloader)

########################################
# Manage bootloader's logs
########################################
if (PFB_WITH_BOOTLOADER_LOGS)
    target_compile_definitions(pico_fota_bootloader PRIVATE PFB_WITH_BOOTLOADER_LOGS)
    if (PFB_REDIRECT_BOOTLOADER_LOGS_TO_UART)
        pico_enable_stdio_usb(pico_fota_bootloader 0)
        pico_enable_stdio_uart(pico_fota_bootloader 1)
    else ()
        pico_enable_stdio_usb(pico_fota_bootloader 1)
        pico_enable_stdio_uart(pico_fota_bootloader 0)
    endif ()
endif ()

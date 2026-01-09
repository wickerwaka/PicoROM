cmake_minimum_required(VERSION 3.13)
set(PICO_PLATFORM,rp2040)
include(pico-sdk/pico_sdk_init.cmake)
project(PicoROM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(VERSION 1.8.0)
string(REPLACE . _ FILENAME_VERSION ${VERSION})

set(PFB_WITH_IMAGE_ENCRYPTION OFF)
set(PFB_WITH_CRC32_HASHING ON)
set(PFB_WITH_SHA256_HASHING OFF)
set(PFB_WITH_BOOTLOADER_LOGS OFF)
set(PFB_RESERVED_FILESYSTEM_SIZE_KB 1024)

pico_sdk_init()

add_subdirectory(pico_fota_bootloader)

target_compile_definitions(pico_fota_bootloader PRIVATE
    PLL_SYS_REFDIV=2
    PLL_SYS_VCO_FREQ_HZ=1350000000
    PLL_SYS_POSTDIV1=5
    PLL_SYS_POSTDIV2=1
    SYS_CLK_HZ=270000000
)
pico_set_program_name(pico_fota_bootloader PicoROM)
pico_set_program_url(pico_fota_bootloader https://github.com/wickerwaka/PicoROM)

foreach(BUILD_CONFIG 2MBit 2MBit_100ns 28P_512KBit)
    set(TARGET PicoROM-${BUILD_CONFIG}-${FILENAME_VERSION})
    add_executable(${TARGET} EXCLUDE_FROM_ALL
        main.cpp
        flash.cpp
        pico_link.cpp
        rom.cpp
        comms.cpp
        pio_programs.cpp
        str_util.cpp
    )
    add_executable(${BUILD_CONFIG} ALIAS ${TARGET})

    pico_set_float_implementation(${TARGET} none)
    pico_set_double_implementation(${TARGET} none)

    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/data_bus.pio)
    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/comms.pio)

    pico_enable_stdio_usb(${TARGET} 1)
    pico_enable_stdio_uart(${TARGET} 0)

    #pico_set_linker_script(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/memmap_firmware.ld)

    pico_add_extra_outputs(${TARGET})
    pfb_compile_with_bootloader(${TARGET})

    target_link_libraries(${TARGET}
        pico_stdlib
        pico_multicore
        pico_bootrom
        hardware_flash
        hardware_pio
        hardware_dma    
        pico_unique_id
        pico_fota_bootloader_lib
        -Wl,--wrap=atexit
    )

    target_compile_definitions(${TARGET} PRIVATE
        PICOROM_CONFIG_NAME="${BUILD_CONFIG}"
        PICOROM_FIRMWARE_VERSION="${VERSION}"
        PICO_HEAP_SIZE=16
        PICO_STACK_SIZE=640
        PICO_CORE1_STACK_SIZE=4
        USB_MAX_ENDPOINTS=4
        PICO_STDIO_ENABLE_CRLF_SUPPORT=0
        PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS=2
        PICO_XOSC_STARTUP_DELAY_MULTIPLIER=4
    )

    target_compile_definitions(${TARGET} PRIVATE
        PLL_SYS_REFDIV=2
        PLL_SYS_VCO_FREQ_HZ=1350000000
        PLL_SYS_POSTDIV1=5
        PLL_SYS_POSTDIV2=1
        SYS_CLK_HZ=270000000
    )
    pico_set_program_name(${TARGET} PicoROM)
    pico_set_program_url(${TARGET} https://github.com/wickerwaka/PicoROM)
    pico_set_program_version(${TARGET} ${VERSION})
endforeach()

target_compile_definitions(PicoROM-2MBit-${FILENAME_VERSION} PRIVATE
    BOARD_32P_TCA=1
)

target_compile_definitions(PicoROM-2MBit_100ns-${FILENAME_VERSION} PRIVATE
    BOARD_32P_TCA=1
    FEATURE_STABLE_ADDRESS=1
)

target_compile_definitions(PicoROM-28P_512KBit-${FILENAME_VERSION} PRIVATE
    BOARD_28P=1
)

set_target_properties(PicoROM-2MBit-${FILENAME_VERSION} PROPERTIES EXCLUDE_FROM_ALL FALSE)
set_target_properties(PicoROM-2MBit_100ns-${FILENAME_VERSION} PROPERTIES EXCLUDE_FROM_ALL FALSE)
set_target_properties(PicoROM-28P_512KBit-${FILENAME_VERSION} PROPERTIES EXCLUDE_FROM_ALL FALSE)


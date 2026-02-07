cmake_minimum_required(VERSION 3.13)
set(PICO_PLATFORM,rp2040)
include(pico-sdk/pico_sdk_init.cmake)
project(PicoROM C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(VERSION 2.0)
string(REPLACE . _ FILENAME_VERSION ${VERSION})

pico_sdk_init()

pico_define_boot_stage2(boot_div4 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(boot_div4 PRIVATE PICO_FLASH_SPI_CLKDIV=4)

set(ALL_FIRMWARE_TARGETS "" CACHE INTERNAL "List of all firmware targets")

# Generate initial build_time.h during configure (will be updated on each build)
execute_process(
    COMMAND ${CMAKE_COMMAND}
        -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/build_time.h
        -P ${CMAKE_CURRENT_LIST_DIR}/generate_build_time.cmake
)

function(picorom BUILD_CONFIG DEFINES)
    set(TARGET PicoROM-${BUILD_CONFIG}-${FILENAME_VERSION})
    add_executable(${TARGET} EXCLUDE_FROM_ALL
        main.cpp
        flash.cpp
        pico_link.cpp
        rom.cpp
        comms.cpp
        pio_programs.cpp
        str_util.cpp
        peripherals.cpp
        usb_descriptors.c
        reset_interface.c
    )

    add_custom_target(${BUILD_CONFIG} DEPENDS ${TARGET})
    target_compile_definitions(${TARGET} PRIVATE ${DEFINES})
    set_target_properties(${TARGET} PROPERTIES EXCLUDE_FROM_ALL FALSE)

    pico_set_float_implementation(${TARGET} none)
    pico_set_double_implementation(${TARGET} none)

    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/data_bus.pio)
    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/comms.pio)

    # Use custom USB implementation (not stdio_usb)
    pico_enable_stdio_usb(${TARGET} 0)
    pico_enable_stdio_uart(${TARGET} 0)

    # Include directory for our tusb_config.h and generated build_time.h
    target_include_directories(${TARGET} PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR})

    # Generate build timestamp on every build
    add_custom_command(TARGET ${TARGET} PRE_BUILD
        COMMAND ${CMAKE_COMMAND}
            -D OUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/build_time.h
            -P ${CMAKE_CURRENT_LIST_DIR}/generate_build_time.cmake
        COMMENT "Generating build timestamp"
    )

    pico_set_linker_script(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/memmap_firmware.ld)

    pico_add_extra_outputs(${TARGET})
    target_link_libraries(${TARGET}
        pico_stdlib
        pico_multicore
        pico_bootrom
        hardware_flash
        hardware_pio
        hardware_dma
        hardware_pwm
        pico_unique_id
        tinyusb_device
        pico_usb_reset_interface_headers
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
    
    pico_set_boot_stage2(${TARGET} boot_div4)
    
    pico_set_program_name(${TARGET} PicoROM)
    pico_set_program_url(${TARGET} https://github.com/wickerwaka/PicoROM)
    pico_set_program_version(${TARGET} ${VERSION})

    set(ALL_FIRMWARE_TARGETS ${ALL_FIRMWARE_TARGETS} ${TARGET} CACHE INTERNAL "List of all firmware targets")
endfunction()

picorom(POG_27C020_70ns BOARD_32P_TCA=1)
picorom(POG_27C020_100ns BOARD_32P_TCA=1 FEATURE_STABLE_ADDRESS=1)
picorom(P32_27C020_70ns BOARD_32P=1)
picorom(P32_27C020_100ns BOARD_32P=1 FEATURE_STABLE_ADDRESS=1)
picorom(P28_27C512_70ns BOARD_28P=1)
picorom(P28_27C512_100ns BOARD_28P=1 FEATURE_STABLE_ADDRESS=1)

foreach(TARGET ${ALL_FIRMWARE_TARGETS})
    list(APPEND FIRMWARE_BIN_FILES "${TARGET}.bin")
endforeach()

add_custom_target(firmware-bundle.zip
    COMMAND ${CMAKE_COMMAND} -E rm -f firmware-bundle.zip
    COMMAND zip -j firmware-bundle.zip ${FIRMWARE_BIN_FILES}
    DEPENDS ${ALL_FIRMWARE_TARGETS}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Creating firmware-bundle.zip"
)


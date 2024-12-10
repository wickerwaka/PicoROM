cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(rom_emulator C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

foreach(TARGET PicoROM)
    add_executable(${TARGET}
        main.cpp
        flash.cpp
        pico_link.cpp
        rom.cpp
        comms.cpp
    )

    pico_set_float_implementation(${TARGET} none)
    pico_set_double_implementation(${TARGET} none)

    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/data_bus.pio)
    pico_generate_pio_header(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/comms.pio)

    pico_enable_stdio_usb(${TARGET} 1)
    pico_enable_stdio_uart(${TARGET} 0)

    pico_set_linker_script(${TARGET} ${CMAKE_CURRENT_LIST_DIR}/memmap_firmware.ld)

    pico_add_extra_outputs(${TARGET})
    target_link_libraries(${TARGET}
        pico_stdlib
        pico_multicore
        hardware_flash
        hardware_pio
        hardware_dma    
        pico_unique_id
    )

    pico_set_program_name(${TARGET} PicoROM)
    pico_set_program_url(${TARGET} https://github.com/wickerwaka/PicoROM)
endforeach()

pico_set_program_version(PicoROM 1.5/1.5)

target_compile_definitions(PicoROM PRIVATE
    PICO_HEAP_SIZE=16
    PICO_STACK_SIZE=640
    PICO_CORE1_STACK_SIZE=4
    USB_MAX_ENDPOINTS=4
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
    PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS=8
)

pico_define_boot_stage2(boot_div4 ${PICO_DEFAULT_BOOT_STAGE2_FILE})
target_compile_definitions(boot_div4 PRIVATE PICO_FLASH_SPI_CLKDIV=4)

pico_set_boot_stage2(PicoROM boot_div4)

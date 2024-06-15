cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(rom_emulator C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(PicoROM
    main.cpp
    pico_link.cpp
    rom.cpp
    comms.cpp
)

pico_set_float_implementation(PicoROM none)
pico_set_double_implementation(PicoROM none)

target_compile_definitions(PicoROM PRIVATE
    PICO_HEAP_SIZE=16
    PICO_STACK_SIZE=640
    PICO_CORE1_STACK_SIZE=4
    USB_MAX_ENDPOINTS=4
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
    TCA_EXPANDER=1
)

pico_generate_pio_header(PicoROM ${CMAKE_CURRENT_LIST_DIR}/data_bus.pio)
pico_generate_pio_header(PicoROM ${CMAKE_CURRENT_LIST_DIR}/comms.pio)

pico_enable_stdio_usb(PicoROM 1)
pico_enable_stdio_uart(PicoROM 0)

pico_set_linker_script(PicoROM ${CMAKE_CURRENT_LIST_DIR}/memmap_firmware.ld)

pico_add_extra_outputs(PicoROM)
target_link_libraries(PicoROM
    pico_stdlib
    pico_multicore
    hardware_flash
    hardware_pio
    pico_unique_id
)

pico_set_program_name(PicoROM PicoROM)
pico_set_program_url(PicoROM https://github.com/wickerwaka/PicoROM)
pico_set_program_version(PicoROM 1.5)
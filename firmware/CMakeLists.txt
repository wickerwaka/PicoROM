cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(rom_emulator C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(picorom
    main.cpp
    pico_link.cpp
    rom.cpp
    comms.cpp
)

pico_set_float_implementation(picorom none)
pico_set_double_implementation(picorom none)

target_compile_definitions(picorom PRIVATE
    PICO_HEAP_SIZE=128
    PICO_STACK_SIZE=512
    PICO_CORE1_STACK_SIZE=4
    USB_MAX_ENDPOINTS=4
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
    OUTPUT_BUFFER=1
)

pico_generate_pio_header(picorom ${CMAKE_CURRENT_LIST_DIR}/data_bus.pio)
pico_generate_pio_header(picorom ${CMAKE_CURRENT_LIST_DIR}/comms.pio)

pico_enable_stdio_usb(picorom 1)
pico_set_linker_script(picorom ${CMAKE_CURRENT_LIST_DIR}/memmap_firmware.ld)

pico_add_extra_outputs(picorom)
target_link_libraries(picorom pico_stdlib pico_multicore hardware_pio)
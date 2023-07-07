cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(rom_emulator C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(firmware
    firmware.cpp
    pico_link.cpp
    state_machines.cpp
)

pico_set_float_implementation(firmware none)
pico_set_double_implementation(firmware none)

target_compile_definitions(firmware PRIVATE
    PICO_HEAP_SIZE=128
    PICO_STACK_SIZE=1024
    PICO_CORE1_STACK_SIZE=4
    USB_MAX_ENDPOINTS=4
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
    OUTPUT_BUFFER=0
)

pico_generate_pio_header(firmware ${CMAKE_CURRENT_LIST_DIR}/data_bus.pio)

pico_enable_stdio_usb(firmware 1)
pico_set_linker_script(firmware ${CMAKE_CURRENT_LIST_DIR}/memmap_firmware.ld)

pico_add_extra_outputs(firmware)
target_link_libraries(firmware pico_stdlib pico_multicore hardware_pio)
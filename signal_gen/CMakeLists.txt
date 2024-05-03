cmake_minimum_required(VERSION 3.13)
include(pico_sdk_import.cmake)
project(rom_emulator C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(PicoROMSignal
    main.cpp
)

pico_enable_stdio_usb(PicoROMSignal 1)

pico_add_extra_outputs(PicoROMSignal)
target_link_libraries(PicoROMSignal
    pico_stdlib
    hardware_pio
)

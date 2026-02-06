cmake_minimum_required(VERSION 3.13)
set(PICO_PLATFORM,rp2040)
include(../firmware/pico-sdk/pico_sdk_init.cmake)
project(testbench C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(testbench
    main.cpp
)


pico_enable_stdio_usb(testbench 1)
pico_enable_stdio_uart(testbench 0)
pico_enable_stdio_rtt(testbench 1)

pico_add_extra_outputs(testbench)
target_link_libraries(testbench
    pico_stdlib
    pico_multicore
)

pico_set_program_name(testbench PicoROMBench)
pico_set_program_url(testbench https://github.com/wickerwaka/PicoROM)

target_compile_definitions(testbench PRIVATE
    PICO_STDIO_ENABLE_CRLF_SUPPORT=0
)

pico_define_boot_stage2(boot_div4 "${PICO_SDK_PATH}/src/rp2040/boot_stage2/boot2_w25x10cl.S")
target_compile_definitions(boot_div4 PRIVATE PICO_FLASH_SPI_CLKDIV=4)

pico_set_boot_stage2(testbench boot_div4)

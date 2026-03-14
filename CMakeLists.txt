cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/usr/src/pico-sdk")
set(PICO_BOARD "pico2")

include(${PICO_SDK_PATH}/pico_sdk_init.cmake)

project(bccc_sao_thermometer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(thermometer
    src/main.c
    src/display.c
)

# Our board has W25Q128 = 16MB flash
target_compile_definitions(thermometer PRIVATE
    PICO_FLASH_SIZE_BYTES=16777216
)

target_include_directories(thermometer PRIVATE src)

target_link_libraries(thermometer
    pico_stdlib
    hardware_gpio
)

# USB serial for debug output; no UART
pico_enable_stdio_usb(thermometer 1)
pico_enable_stdio_uart(thermometer 0)

# Generate UF2 (drag-and-drop flash via USB bootloader) and map/bin
pico_add_extra_outputs(thermometer)

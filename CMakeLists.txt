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
    src/ds18b20.c
    src/temp.c
)

# Hardware revision: 10 = v1.0, 13 = v1.3 (brightness button on GPIO 19)
set(HW_REV 13)

target_compile_definitions(thermometer PRIVATE
    PICO_FLASH_SIZE_BYTES=2097152   # 2MB — fits both W25Q16 and W25Q128
    HW_REV=${HW_REV}
)

target_include_directories(thermometer PRIVATE src)

target_link_libraries(thermometer
    pico_stdlib
    hardware_gpio
    hardware_adc
    pico_multicore
)

# USB serial for debug output; no UART
pico_enable_stdio_usb(thermometer 1)
pico_enable_stdio_uart(thermometer 0)

# Generate UF2 (drag-and-drop flash via USB bootloader) and map/bin
pico_add_extra_outputs(thermometer)

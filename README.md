# BCCC SAO Temperature Badge

A Simple Add-On (SAO) badge for the DEF CON Beverage Chilling Contraption Contest (BCCC). Winners of the contest receive copies of this badge, at least for this year (2026) or until I run out.

The PCB is shaped like a red solo cup with overflowing beer. It reads ambient temperature using a DS18B20 sensor and displays the result in degrees Fahrenheit on three 7-segment LED displays.

## Hardware

- **MCU:** Raspberry Pi RP2350A (QFN-60)
- **Flash:** W25Q128 (16 MB)
- **Sensor:** DS18B20 1-Wire temperature sensor (GPIO 16); falls back to the RP2350's internal die sensor if not present
- **Display:** 3x common-anode 7-segment LED displays, multiplexed via a 74HC595 shift register
- **Interface:** USB-C (programming and serial debug output)
- **Extras:** 3-pin SWD debug header, 2 user pushbuttons (RUN and QSPI_SS)

### Pin Assignments

| Signal | GPIO |
|---|---|
| 1-Wire (DS18B20) | 16 |
| Digit select 1 (leftmost) | 8 |
| Digit select 2 (middle) | 9 |
| Digit select 3 (rightmost) | 10 |
| 74HC595 SER (data) | 11 |
| 74HC595 SRCLK (shift clock) | 12 |
| 74HC595 RCLK (latch clock) | 13 |
| 74HC595 /OE (output enable) | 14 |
| 74HC595 /MR (master reset) | 15 |

## Firmware

The firmware uses the Raspberry Pi Pico SDK and is structured around both RP2350 cores:

- **Core 0** — reads temperature from the DS18B20 every ~1 second and updates the display buffer
- **Core 1** — continuously time-multiplexes the three 7-segment digits at ~333 Hz

If no DS18B20 is detected on the 1-Wire bus, the display shows `---` and the internal die temperature is logged to USB serial for reference.

### Prerequisites

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) installed at `/usr/src/pico-sdk` (or update `PICO_SDK_PATH` in `CMakeLists.txt`)
- CMake 3.13+
- ARM GCC toolchain (`gcc-arm-none-eabi`)

On Debian/Ubuntu:

```sh
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
```

### Building

```sh
mkdir build && cd build
cmake ..
make -j$(nproc)
```

This produces `build/thermometer.uf2`.

### Flashing

1. Hold the BOOTSEL button (QSPI_SS) while plugging in USB-C, or press and hold QSPI_SS then press RUN to reboot into bootloader mode.
2. The board will appear as a USB mass storage device (`RPI-RP2`).
3. Copy `thermometer.uf2` to the drive:

```sh
cp build/thermometer.uf2 /media/$USER/RPI-RP2/
```

The board reboots automatically and starts running.

### Serial Debug Output

Connect a serial terminal at any baud rate (USB CDC ACM) to see temperature readings:

```sh
screen /dev/ttyACM0
# or
minicom -D /dev/ttyACM0
```

## Hardware Design

The KiCad project files are in `hardware/bccc_sao_rp2350a/`. The PCB artwork — a red solo cup with overflowing beer — was designed in [Inkscape](https://inkscape.org/), starting from an AI-generated image, then transferred into KiCad using the [SVG2Shenzhen](https://github.com/badgeek/svg2shenzhen) plugin.

Many thanks to the [KiCad](https://www.kicad.org/) project for excellent, free, open-source EDA software.

## License

[WTFPL v2](LICENSE) — do what the f*** you want with it.

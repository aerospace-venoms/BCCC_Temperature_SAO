# Prebuilt Firmware

Ready-to-flash firmware images for the BCCC Temperature SAO. Use these if you
just want to (re)flash your badge without building from source.

| File | Version | Hardware |
|---|---|---|
| `thermometer-v1.0.1.uf2` | 1.0.1 | RP2350A, HW rev ≥ 1.3 |

## Flashing

1. Hold the **BOOTSEL** button (QSPI_SS) while plugging in USB-C — the board
   mounts as a USB drive named `RP2350`.
2. Copy the `.uf2` onto that drive:

   ```sh
   cp firmware/thermometer-v1.0.1.uf2 /media/$USER/RP2350/
   ```

3. The board reboots and runs automatically.

The running firmware reports its version over USB serial at boot:

```
BCCC Temperature SAO — firmware v1.0.1 (HW rev 13)
```

To rebuild this image from source, see the top-level [README](../README.md).
The version string lives in `CMakeLists.txt` (`FW_VERSION`).

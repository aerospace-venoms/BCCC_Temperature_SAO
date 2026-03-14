#include "ds18b20.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"

// ---------------------------------------------------------------------------
// 1-Wire timing (all values in microseconds)
// DS18B20 datasheet: Fig. 14 / 15 (reset/presence), Fig. 16 (read/write slots)
//
// sleep_us() on RP2350 has ~1-2 µs of call overhead, so all targets are set
// conservatively to stay within spec after that overhead is added.
// ---------------------------------------------------------------------------
#define OW_RESET_US         500   // reset pulse width (spec min 480 µs)
#define OW_PRESENCE_WAIT_US  80   // master release → sample presence
#define OW_PRESENCE_TAIL_US 420   // remainder of reset recovery (500-80)
#define OW_SLOT_US           70   // total read/write slot (spec min 60 µs)
#define OW_WRITE1_LOW_US      8   // low time for write-1 slot (spec 1–15 µs)
#define OW_WRITE0_LOW_US     65   // low time for write-0 slot (spec 60–120 µs)
#define OW_READ_LOW_US        5   // initiate read slot (spec 1–15 µs)
#define OW_READ_SAMPLE_US    13   // sample at 13 µs after slot start (must be <15)

// DS18B20 commands
#define CMD_SKIP_ROM         0xCC
#define CMD_CONVERT_T        0x44
#define CMD_READ_SCRATCHPAD  0xBE

// 12-bit conversion time (ms)
#define CONV_TIME_MS         750

// ---------------------------------------------------------------------------
// CRC-8/Maxim (polynomial 0x31, reflected input/output, init 0x00)
// Computing crc8(buf, n) where buf[n-1] is the CRC byte should return 0.
// ---------------------------------------------------------------------------
static uint8_t crc8(const uint8_t *data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; i++) {
        uint8_t byte = data[i];
        for (int b = 0; b < 8; b++) {
            if ((crc ^ byte) & 0x01)
                crc = (crc >> 1) ^ 0x8C;
            else
                crc >>= 1;
            byte >>= 1;
        }
    }
    return crc;
}

// ---------------------------------------------------------------------------
// Bus primitives
// ---------------------------------------------------------------------------

static inline void ow_release(void) {
    // Let internal pull-up bring the line high (open-drain idle state)
    gpio_set_dir(PIN_OW, GPIO_IN);
}

static inline void ow_drive_low(void) {
    gpio_put(PIN_OW, 0);
    gpio_set_dir(PIN_OW, GPIO_OUT);
}

// Returns true if a device responded with a presence pulse.
static bool ow_reset(void) {
    ow_drive_low();
    sleep_us(OW_RESET_US);
    ow_release();
    sleep_us(OW_PRESENCE_WAIT_US);
    bool present = !gpio_get(PIN_OW);   // device pulls low = present
    sleep_us(OW_PRESENCE_TAIL_US);
    return present;
}

static void ow_write_bit(bool bit) {
    if (bit) {
        ow_drive_low();
        sleep_us(OW_WRITE1_LOW_US);
        ow_release();
        sleep_us(OW_SLOT_US - OW_WRITE1_LOW_US);
    } else {
        ow_drive_low();
        sleep_us(OW_WRITE0_LOW_US);
        ow_release();
        sleep_us(OW_SLOT_US - OW_WRITE0_LOW_US);
    }
}

static void ow_write_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++)
        ow_write_bit((byte >> i) & 1);  // LSB first per 1-Wire spec
}

static bool ow_read_bit(void) {
    ow_drive_low();
    sleep_us(OW_READ_LOW_US);
    ow_release();
    sleep_us(OW_READ_SAMPLE_US - OW_READ_LOW_US);
    bool bit = gpio_get(PIN_OW);
    sleep_us(OW_SLOT_US - OW_READ_SAMPLE_US);
    return bit;
}

static uint8_t ow_read_byte(void) {
    uint8_t byte = 0;
    for (int i = 0; i < 8; i++)
        if (ow_read_bit()) byte |= (1 << i);   // LSB first
    return byte;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ds18b20_init(void) {
    gpio_init(PIN_OW);
    gpio_pull_up(PIN_OW);       // ~50 kΩ internal pull-up — adequate for short PCB trace
    gpio_set_dir(PIN_OW, GPIO_IN);
}

bool ds18b20_detect(void) {
    return ow_reset();
}

bool ds18b20_read_temp_c(float *temp_c) {
    // Trigger conversion
    if (!ow_reset()) return false;
    ow_write_byte(CMD_SKIP_ROM);
    ow_write_byte(CMD_CONVERT_T);

    // Wait for 12-bit conversion to complete.
    // The display is being multiplexed on core 1, so blocking here is fine.
    sleep_ms(CONV_TIME_MS);

    // Read scratchpad
    if (!ow_reset()) return false;
    ow_write_byte(CMD_SKIP_ROM);
    ow_write_byte(CMD_READ_SCRATCHPAD);

    uint8_t sp[9];
    for (int i = 0; i < 9; i++)
        sp[i] = ow_read_byte();

    // Validate CRC: crc8 over all 9 bytes (including the CRC byte) must be 0
    if (crc8(sp, 9) != 0) return false;

    // sp[0] = temp LSB, sp[1] = temp MSB (two's-complement, 1/16 °C per LSB)
    int16_t raw = (int16_t)((sp[1] << 8) | sp[0]);
    float t = raw / 16.0f;

    // DS18B20 valid range is -55 °C to +125 °C; reject readings outside this
    if (t < -55.0f || t > 125.0f) return false;

    *temp_c = t;
    return true;
}

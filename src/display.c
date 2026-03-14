#include "display.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <math.h>

// Segment patterns for '0'–'9' (no decimal point, active-high segments)
//   bit 0=a, 1=b, 2=c, 3=d, 4=e, 5=f, 6=g
const uint8_t SEG_DIGITS[10] = {
    0x3F, // 0: a b c d e f
    0x06, // 1: b c
    0x5B, // 2: a b d e g
    0x4F, // 3: a b c d g
    0x66, // 4: b c f g
    0x6D, // 5: a c d f g
    0x7D, // 6: a c d e f g
    0x07, // 7: a b c
    0x7F, // 8: a b c d e f g
    0x6F, // 9: a b c d f g
};

// Shadow buffer – segment bytes for digits 0, 1, 2 (left to right)
static uint8_t s_buf[NUM_DIGITS] = {SEG_BLANK, SEG_BLANK, SEG_BLANK};

static const uint8_t DIG_PINS[NUM_DIGITS] = {PIN_DIG1, PIN_DIG2, PIN_DIG3};

// ---------------------------------------------------------------------------
// Low-level shift register helpers
// ---------------------------------------------------------------------------

static inline void sr_clock_pulse(void) {
    gpio_put(PIN_SR_SRCLK, 1);
    gpio_put(PIN_SR_SRCLK, 0);
}

static inline void sr_latch(void) {
    gpio_put(PIN_SR_RCLK, 1);
    gpio_put(PIN_SR_RCLK, 0);
}

// ---------------------------------------------------------------------------
// Public implementation
// ---------------------------------------------------------------------------

void display_init(void) {
    // Shift register pins
    const uint8_t sr_pins[] = {
        PIN_SR_DATA, PIN_SR_SRCLK, PIN_SR_RCLK, PIN_SR_OE, PIN_SR_MR
    };
    for (int i = 0; i < 5; i++) {
        gpio_init(sr_pins[i]);
        gpio_set_dir(sr_pins[i], GPIO_OUT);
        gpio_put(sr_pins[i], 0);
    }

    // Digit select pins
    for (int i = 0; i < NUM_DIGITS; i++) {
        gpio_init(DIG_PINS[i]);
        gpio_set_dir(DIG_PINS[i], GPIO_OUT);
        gpio_put(DIG_PINS[i], 0); // all digits off
    }

    // Release shift register from reset, then enable outputs
    gpio_put(PIN_SR_MR, 1);  // /MR HIGH = normal operation
    gpio_put(PIN_SR_OE, 0);  // /OE LOW  = outputs enabled

    // Clock out 8 zeros to blank the display
    display_sr_write(SEG_BLANK);
}

void display_sr_write(uint8_t segments) {
    // Send MSB first; after 8 shifts, bit 0 lands at QA (segment a), bit 7 at QH (DP)
    for (int bit = 7; bit >= 0; bit--) {
        gpio_put(PIN_SR_DATA, (segments >> bit) & 1);
        sr_clock_pulse();
    }
    sr_latch();
}

void display_set_raw(uint8_t d0, uint8_t d1, uint8_t d2) {
    s_buf[0] = d0;
    s_buf[1] = d1;
    s_buf[2] = d2;
}

void display_set_number(int value) {
    if (value < 0 || value > 999) {
        s_buf[0] = SEG_DASH;
        s_buf[1] = SEG_DASH;
        s_buf[2] = SEG_DASH;
        return;
    }
    s_buf[0] = SEG_DIGITS[value / 100];
    s_buf[1] = SEG_DIGITS[(value / 10) % 10];
    s_buf[2] = SEG_DIGITS[value % 10];
}

void display_set_digit(int pos, uint8_t segments) {
    if (pos < 0 || pos >= NUM_DIGITS) return;
    for (int i = 0; i < NUM_DIGITS; i++)
        s_buf[i] = SEG_BLANK;
    s_buf[pos] = segments;
}

void display_refresh(int digit) {
    if (digit < 0 || digit >= NUM_DIGITS) return;

    // Turn all digits off before changing segment data to prevent ghosting
    for (int i = 0; i < NUM_DIGITS; i++)
        gpio_put(DIG_PINS[i], 0);

    // Shift in the segment pattern for this digit
    display_sr_write(s_buf[digit]);

    // Enable this digit
    gpio_put(DIG_PINS[digit], 1);
}

void display_set_temp_f(float temp_f) {
    if (isnan(temp_f) || temp_f < -99.9f || temp_f > 999.9f) {
        s_buf[0] = SEG_DASH;
        s_buf[1] = SEG_DASH;
        s_buf[2] = SEG_DASH;
        return;
    }

    if (temp_f >= 100.0f) {
        // Integer display: "NNN"
        int t = (int)(temp_f + 0.5f);
        if (t > 999) t = 999;
        s_buf[0] = SEG_DIGITS[t / 100];
        s_buf[1] = SEG_DIGITS[(t / 10) % 10];
        s_buf[2] = SEG_DIGITS[t % 10];
    } else if (temp_f >= 0.0f) {
        // One decimal place: "XX.X"
        int t = (int)(temp_f * 10.0f + 0.5f);   // e.g. 72.5 → 725
        s_buf[0] = SEG_DIGITS[t / 100];
        s_buf[1] = SEG_DIGITS[(t / 10) % 10] | SEG_DP;
        s_buf[2] = SEG_DIGITS[t % 10];
    } else {
        // Negative: "-XX" (no decimal; below 0 °F is unusual but handle it)
        int t = (int)(-temp_f + 0.5f);
        if (t > 99) t = 99;
        s_buf[0] = SEG_DASH;
        s_buf[1] = SEG_DIGITS[t / 10];
        s_buf[2] = SEG_DIGITS[t % 10];
    }
}

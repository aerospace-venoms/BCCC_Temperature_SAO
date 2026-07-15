// Step 2: Temperature sensing with DS18B20 (or RP2350 internal fallback)
//
// Core 0: reads temperature every ~1 s and updates the display buffer.
// Core 1: time-multiplexes the 3-digit display continuously at ~333 Hz.
//
// Display behaviour:
//   DS18B20 present  → temperature in °F on the 7-segment display
//   DS18B20 absent   → "---" on the display; internal die temp logged to serial

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "display.h"
#include "temp.h"
#include "ds18b20.h"    // for PIN_OW
#include <stdio.h>

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"   // fallback if not set by the build
#endif

#define DIGIT_ON_US 333     // ~333 µs per digit → ~1 kHz full refresh

// ---------------------------------------------------------------------------
// Brightness control (HW_REV >= 13: button on GPIO 19)
// ---------------------------------------------------------------------------
#if HW_REV >= 13

#define PIN_BRIGHT_BTN  19
// Brightness levels as percentage of DIGIT_ON_US.
// Easy to tweak: just edit this array.
static const uint8_t BRIGHT_PCT[] = { 100, 66, 33, 15 };
#define NUM_BRIGHT_LEVELS (sizeof(BRIGHT_PCT) / sizeof(BRIGHT_PCT[0]))

static volatile int s_bright_idx = 0;

#define DEBOUNCE_US 20000   // 20 ms debounce window

static void brightness_isr(uint gpio, uint32_t events) {
    (void)events;
    if (gpio != PIN_BRIGHT_BTN) return;

    static uint64_t last_us = 0;
    uint64_t now = time_us_64();
    if (now - last_us < DEBOUNCE_US) return;
    last_us = now;

    s_bright_idx = (s_bright_idx + 1) % NUM_BRIGHT_LEVELS;
}

static void brightness_init(void) {
    gpio_init(PIN_BRIGHT_BTN);
    gpio_set_dir(PIN_BRIGHT_BTN, GPIO_IN);
    gpio_pull_up(PIN_BRIGHT_BTN);
    gpio_set_irq_enabled_with_callback(PIN_BRIGHT_BTN,
        GPIO_IRQ_EDGE_FALL, true, brightness_isr);
}

#endif // HW_REV >= 13

// ---------------------------------------------------------------------------
// Core 1: display multiplexer
// ---------------------------------------------------------------------------
static void core1_display_loop(void) {
    int digit = 0;
    while (true) {
#if HW_REV >= 13
        int on_us = DIGIT_ON_US * BRIGHT_PCT[s_bright_idx] / 100;
        display_refresh(digit);
        sleep_us(on_us);
        // Blank remainder of time slot for dimming
        for (int i = 0; i < NUM_DIGITS; i++)
            gpio_put(8 + i, 0);    // PIN_DIG1..3 = GPIO 8..10
        sleep_us(DIGIT_ON_US - on_us);
#else
        display_refresh(digit);
        sleep_us(DIGIT_ON_US);
#endif
        if (++digit >= NUM_DIGITS) digit = 0;
    }
}

// ---------------------------------------------------------------------------
// Core 0: application
// ---------------------------------------------------------------------------
int main(void) {
    stdio_init_all();
    sleep_ms(1000);     // give USB CDC time to enumerate

    printf("BCCC Temperature SAO — firmware v%s (HW rev %d)\n", FW_VERSION, HW_REV);

    display_init();
    display_set_raw(SEG_DASH, SEG_DASH, SEG_DASH);

    multicore_launch_core1(core1_display_loop);

    // Boot splash: "DEF" → "C0n" → "LoL"
    display_set_raw(SEG_CHR_D, SEG_CHR_E, SEG_CHR_F);
    sleep_ms(1000);
    display_set_raw(SEG_CHR_C, SEG_DIGITS[0], SEG_CHR_n);
    sleep_ms(1000);
    display_set_raw(SEG_CHR_L, SEG_CHR_o, SEG_CHR_L);
    sleep_ms(1000);

#if HW_REV >= 13
    brightness_init();
#endif

    temp_init();

    if (temp_has_ds18b20()) {
        printf("Sensor: DS18B20 on GPIO %d\n", PIN_OW);
    } else {
        printf("Sensor: DS18B20 not found on GPIO %d\n", PIN_OW);
        printf("        Displaying --- ; internal die temp logged here.\n");
    }

    while (true) {
        if (temp_has_ds18b20()) {
            float f = temp_read_f();
            display_set_temp_f(f);
            printf("DS18B20:  %.1f F\n", f);
        } else {
            // No external sensor — keep "---" on the display.
            // The internal sensor is not an ambient thermometer, so we
            // log it to serial for information only.
            display_set_raw(SEG_DASH, SEG_DASH, SEG_DASH);
            printf("Internal: %.1f F  (die temp, not ambient)\n",
                   temp_read_internal_f());
        }
        sleep_ms(100);
    }
}

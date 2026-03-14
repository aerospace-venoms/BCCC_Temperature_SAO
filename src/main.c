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

#define DIGIT_ON_US 1000    // 1 ms per digit → ~333 Hz full refresh

// ---------------------------------------------------------------------------
// Core 1: display multiplexer
// ---------------------------------------------------------------------------
static void core1_display_loop(void) {
    int digit = 0;
    while (true) {
        display_refresh(digit);
        sleep_us(DIGIT_ON_US);
        if (++digit >= NUM_DIGITS) digit = 0;
    }
}

// ---------------------------------------------------------------------------
// Core 0: application
// ---------------------------------------------------------------------------
int main(void) {
    stdio_init_all();
    sleep_ms(1000);     // give USB CDC time to enumerate

    display_init();
    display_set_raw(SEG_DASH, SEG_DASH, SEG_DASH);

    multicore_launch_core1(core1_display_loop);

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
        sleep_ms(1000);
    }
}

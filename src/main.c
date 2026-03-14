// Step 2: Temperature sensing with DS18B20 (or RP2350 internal fallback)
//
// Core 0: reads temperature every ~1 s and updates the display buffer.
// Core 1: time-multiplexes the 3-digit display continuously at ~333 Hz,
//         so the display stays lit even while core 0 blocks during the
//         750 ms DS18B20 conversion.
//
// USB serial reports which sensor is active and prints each reading.

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "display.h"
#include "temp.h"
#include "ds18b20.h"    // for PIN_OW
#include <stdio.h>

#define DIGIT_ON_US 1000    // 1 ms per digit → ~333 Hz full refresh

// ---------------------------------------------------------------------------
// Core 1: display multiplexer (runs forever, no inter-core sync needed —
// the display buffer is updated atomically one byte at a time by core 0,
// and a single torn frame is imperceptible at 333 Hz)
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
    sleep_ms(1000);     // give USB CDC time to enumerate before first printf

    display_init();
    display_set_raw(SEG_DASH, SEG_DASH, SEG_DASH);  // show "---" until first reading

    multicore_launch_core1(core1_display_loop);

    temp_init();

    if (temp_has_ds18b20()) {
        printf("Sensor: DS18B20 on GPIO %d\n", PIN_OW);
    } else {
        printf("Sensor: DS18B20 not found — using RP2350 internal ADC\n");
        printf("        (connect DS18B20 to GPIO %d and reset to activate it)\n", PIN_OW);
    }

    while (true) {
        float f = temp_read_f();
        printf("%.1f F\n", f);
        display_set_temp_f(f);
        sleep_ms(1000);
    }
}

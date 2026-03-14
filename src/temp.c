#include "temp.h"
#include "ds18b20.h"
#include "hardware/adc.h"
#include <math.h>

static bool s_has_ds18b20 = false;

void temp_init(void) {
    // Always prepare the internal ADC so it's ready as a fallback.
    adc_init();
    adc_set_temp_sensor_enabled(true);

    ds18b20_init();
    s_has_ds18b20 = ds18b20_detect();
}

bool temp_has_ds18b20(void) {
    return s_has_ds18b20;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static float read_ds18b20_c(void) {
    float t;
    if (!ds18b20_read_temp_c(&t)) {
        // Lost contact mid-session — fall back to internal sensor from now on.
        s_has_ds18b20 = false;
        return NAN;
    }
    return t;
}

static float read_internal_c(void) {
    // RP2350 internal temperature sensor on ADC channel 4.
    // Formula from RP2350 datasheet: T(°C) = 27 - (Vadc - 0.706) / 0.001721
    adc_select_input(4);
    uint16_t raw = adc_read();
    float voltage = raw * (3.3f / 4096.0f);
    return 27.0f - (voltage - 0.706f) / 0.001721f;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

float temp_read_c(void) {
    if (s_has_ds18b20) {
        float t = read_ds18b20_c();
        if (!isnan(t)) return t;
        // fell through: s_has_ds18b20 is now false, use internal
    }
    return read_internal_c();
}

float temp_read_f(void) {
    return temp_read_c() * 9.0f / 5.0f + 32.0f;
}

#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PIN_OW  16  // 1-Wire bus GPIO

// Initialise the GPIO (input + internal pull-up).
void ds18b20_init(void);

// Send a reset pulse and listen for a presence pulse.
// Returns true if a device responds.
bool ds18b20_detect(void);

// Trigger a 12-bit temperature conversion and read the result.
// Blocks ~750 ms while the sensor converts.
// Returns true and writes *temp_c on success; returns false if the
// device does not respond (bus error or not connected).
bool ds18b20_read_temp_c(float *temp_c);

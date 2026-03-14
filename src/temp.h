#pragma once
#include <stdbool.h>

// Initialise whichever sensor is available.
// Probes for DS18B20 first; if absent, falls back to the RP2350 internal
// ADC temperature sensor.  Call once at startup.
void temp_init(void);

// True if a DS18B20 was detected at startup.
bool temp_has_ds18b20(void);

// Read the current temperature in degrees Celsius.
float temp_read_c(void);

// Read the current temperature in degrees Fahrenheit.
float temp_read_f(void);

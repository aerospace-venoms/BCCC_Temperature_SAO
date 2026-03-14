#pragma once
#include <stdint.h>
#include <stdbool.h>

// ---------------------------------------------------------------------------
// 74HC595 shift register pin assignments
// ---------------------------------------------------------------------------
#define PIN_SR_DATA     11  // SER  – serial data in
#define PIN_SR_SRCLK    12  // SRCLK – shift register clock
#define PIN_SR_RCLK     13  // RCLK  – storage register (latch) clock
#define PIN_SR_OE       14  // /OE   – output enable (active low, pull LOW to enable)
#define PIN_SR_MR       15  // /MR   – master reset  (active low, pull HIGH to run)

// ---------------------------------------------------------------------------
// Digit select pins (active HIGH via N-MOS; HIGH = digit anode energised)
// ---------------------------------------------------------------------------
#define PIN_DIG1        8   // leftmost digit
#define PIN_DIG2        9   // middle digit
#define PIN_DIG3        10  // rightmost digit
#define NUM_DIGITS      3

// ---------------------------------------------------------------------------
// Segment byte encoding
// Bit position → 74HC595 output → display segment
//   bit 0 → QA → segment a (top)
//   bit 1 → QB → segment b (top-right)
//   bit 2 → QC → segment c (bottom-right)
//   bit 3 → QD → segment d (bottom)
//   bit 4 → QE → segment e (bottom-left)
//   bit 5 → QF → segment f (top-left)
//   bit 6 → QG → segment g (middle)
//   bit 7 → QH → decimal point
//
// Sent MSB-first into SER; after 8 SRCLK pulses the last bit ends up at QA.
// ---------------------------------------------------------------------------
#define SEG_DP  (1 << 7)

// Segment patterns for '0'–'9' (no decimal point)
extern const uint8_t SEG_DIGITS[10];

// Segment patterns for a few extra glyphs
#define SEG_DASH    0x40    // segment g only  ( – )
#define SEG_ALL_ON  0x7F    // all segments, no DP
#define SEG_BLANK   0x00    // all off

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Initialise all display GPIOs, reset/enable the shift register.
void display_init(void);

// Write one byte to the shift register and latch it.
void display_sr_write(uint8_t segments);

// Set the raw segment byte for each digit (index 0 = leftmost).
// This does NOT change the display immediately – call display_refresh() in
// your multiplex loop.
void display_set_raw(uint8_t d0, uint8_t d1, uint8_t d2);

// Show an integer 0–999. Values outside range show "---".
void display_set_number(int value);

// Show one digit on one digit position (0–2), all others blank.
// Useful for testing individual positions.
void display_set_digit(int pos, uint8_t segments);

// Drive one multiplex frame: turn off all digits, shift in segments for
// `digit` (0–2), turn that digit on.  Call this in a tight loop at >1 kHz
// per digit (i.e. cycle through all three digits faster than ~300 Hz total).
void display_refresh(int digit);

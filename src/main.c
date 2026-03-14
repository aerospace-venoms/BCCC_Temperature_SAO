// Step 1b: Diagnostic sequence for 7-segment display bringup
//
// Sequence (each phase held for ~2 seconds, then repeats):
//
//  Phase 1  "888"          – all segments, no DP  (wiring sanity, baseline)
//  Phase 2  "8.8.8."       – all segments + all DPs
//  Phase 3  "1  "          – digit 0 only, numeral 1
//  Phase 4  " 2 "          – digit 1 only, numeral 2
//  Phase 5  "  3"          – digit 2 only, numeral 3
//  Phase 6  "123"          – all three digits, independent values
//  Phase 7  segment sweep  – segments a→b→c→d→e→f→g→dp, one at a time
//                            on all three digits simultaneously (~0.5 s each)
//  Phase 8  count 0–999    – normal counting to verify full display

#include "pico/stdlib.h"
#include "display.h"

#define DIGIT_ON_US 1000   // 1 ms per digit → ~333 Hz full refresh

static void multiplex_for_ms(int duration_ms) {
    int cycles = (duration_ms * 1000) / (DIGIT_ON_US * NUM_DIGITS);
    if (cycles < 1) cycles = 1;
    for (int c = 0; c < cycles; c++) {
        for (int d = 0; d < NUM_DIGITS; d++) {
            display_refresh(d);
            sleep_us(DIGIT_ON_US);
        }
    }
}

int main(void) {
    stdio_init_all();
    display_init();

    while (true) {

        // Phase 1: all segments, no DP
        display_set_raw(SEG_ALL_ON, SEG_ALL_ON, SEG_ALL_ON);
        multiplex_for_ms(2000);

        // Phase 2: all segments + decimal points
        display_set_raw(SEG_ALL_ON | SEG_DP, SEG_ALL_ON | SEG_DP, SEG_ALL_ON | SEG_DP);
        multiplex_for_ms(2000);

        // Phase 3: digit 0 only (leftmost) shows "1"
        display_set_raw(SEG_DIGITS[1], SEG_BLANK, SEG_BLANK);
        multiplex_for_ms(2000);

        // Phase 4: digit 1 only (middle) shows "2"
        display_set_raw(SEG_BLANK, SEG_DIGITS[2], SEG_BLANK);
        multiplex_for_ms(2000);

        // Phase 5: digit 2 only (rightmost) shows "3"
        display_set_raw(SEG_BLANK, SEG_BLANK, SEG_DIGITS[3]);
        multiplex_for_ms(2000);

        // Phase 6: all three digits, different values "1 2 3"
        display_set_raw(SEG_DIGITS[1], SEG_DIGITS[2], SEG_DIGITS[3]);
        multiplex_for_ms(2000);

        // Phase 7: segment sweep – one bit at a time, all digits same pattern
        // Lets us verify each shift-register output maps to the right segment
        const char *seg_names[] = {"a","b","c","d","e","f","g","dp"};
        (void)seg_names;
        for (int bit = 0; bit < 8; bit++) {
            uint8_t pat = (1 << bit);
            display_set_raw(pat, pat, pat);
            multiplex_for_ms(600);
        }

        // Phase 8: count 000–999
        for (int n = 0; n <= 999; n++) {
            display_set_number(n);
            multiplex_for_ms(80);
        }
    }
}

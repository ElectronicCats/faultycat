#pragma once

#include <stdbool.h>
#include <stdint.h>

// drivers/ui_leds — the three user-facing LEDs on the FaultyCat v2.x
// PCB. See docs/HARDWARE_V2.md §5 for the physical meaning of each
// indicator and the maintainer's confirmation of their pin mapping.
//
// Scope: init / set / get + an anti-flicker hysteresis helper for the
// HV-DETECTED LED (ported from the legacy firmware's 500 ms hold).
//
// Policy lives above this layer — services decide *when* to turn
// LEDs on, this driver just drives the pins.

typedef enum {
    UI_LED_HV_DETECTED = 0, // GP9
    UI_LED_STATUS      = 1, // GP10
    UI_LED_CHARGE_ON   = 2, // GP27
    UI_LED_COUNT       = 3,
} ui_led_t;

// Configure all three LEDs as GPIO outputs, start them all off.
// Must be called before any ui_leds_* call returns meaningful output.
void ui_leds_init(void);

// Drive `led` high (on) or low (off). Out-of-range ids are a no-op.
void ui_leds_set(ui_led_t led, bool on);

// Return the last value driven into `led`. Shadow state, not a pin
// read — callers care about intent, not electrical reality.
bool ui_leds_get(ui_led_t led);

// How long the HV-DETECTED LED stays on after the last "charged"
// sample, in milliseconds. Matches the legacy behaviour exactly so
// the user experience is preserved.
#define UI_LEDS_HV_HOLD_MS 500u

// Drive the HV-DETECTED LED with hysteresis. Call on every service
// loop tick with the latest reading of the HV-CHARGED feedback:
//   - if `charged_now` is true, turn the LED on immediately and
//     remember this moment.
//   - if false, only turn the LED off after UI_LEDS_HV_HOLD_MS
//     elapsed since the last true-sample.
// Ported from firmware/c/main.c::picoemp_process_charging.
void ui_leds_hv_detected_feed(bool charged_now);

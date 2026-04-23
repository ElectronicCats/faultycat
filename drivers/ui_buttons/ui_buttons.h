#pragma once

#include <stdbool.h>

// drivers/ui_buttons — the two front-panel buttons on the FaultyCat v2.x.
//
// Physical wiring (see docs/HARDWARE_V2.md):
//   ARM   (GP28) — active-high, requires internal pulldown.
//   PULSE (GP11) — active-low,  requires internal pullup.
//
// The legacy firmware used the RP2040's `gpio_set_inover` override to
// flip the PULSE reading in hardware so both buttons read the same
// polarity. We instead invert in software inside this driver, keeping
// the HAL free of RP2040-specific override knobs. The user-facing
// contract is the same: `ui_buttons_is_pressed()` returns true exactly
// when the button is physically pressed.

typedef enum {
    UI_BTN_ARM   = 0, // GP28, active-high
    UI_BTN_PULSE = 1, // GP11, active-low
    UI_BTN_COUNT = 2,
} ui_btn_t;

// Configure both buttons: input + correct internal pull. Debouncing is
// NOT done here — the policy owner (UI service, diag loop) polls at
// its own cadence and decides how to debounce or edge-detect.
void ui_buttons_init(void);

// Return true iff the button is currently pressed. Polarity is
// normalized: both buttons return `true` when physically pressed,
// regardless of whether they're wired active-high or active-low.
bool ui_buttons_is_pressed(ui_btn_t btn);

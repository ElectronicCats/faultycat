// FaultyCat v3 — firmware entrypoint.
//
// F2a commit 1 replaces the F1 single-LED blink with a sweep across
// the three LEDs on v2.2, using the new drivers/ui_leds/ API. Nothing
// but the driver and the HAL is linked in — same layering rule as
// before.
//
// The visible pattern for this phase is a left-to-right chase:
//   HV_DETECTED on → STATUS on → CHARGE_ON on → pause → repeat.
// When you flash this UF2, every LED should blink in turn; if one
// stays dark, that's a pin-mapping bug — tell the maintainer.

#include "hal/time.h"
#include "ui_leds.h"

#define CHASE_STEP_MS 300u

int main(void) {
    ui_leds_init();

    while (true) {
        ui_leds_set(UI_LED_HV_DETECTED, true);
        hal_sleep_ms(CHASE_STEP_MS);
        ui_leds_set(UI_LED_HV_DETECTED, false);

        ui_leds_set(UI_LED_STATUS, true);
        hal_sleep_ms(CHASE_STEP_MS);
        ui_leds_set(UI_LED_STATUS, false);

        ui_leds_set(UI_LED_CHARGE_ON, true);
        hal_sleep_ms(CHASE_STEP_MS);
        ui_leds_set(UI_LED_CHARGE_ON, false);

        hal_sleep_ms(CHASE_STEP_MS);
    }
}

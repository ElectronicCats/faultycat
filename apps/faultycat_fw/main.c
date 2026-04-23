// FaultyCat v3 — firmware entrypoint.
//
// F1 rewrites the F0 blink on top of the HAL. No pico-sdk symbols
// appear in this file any more — the only way to reach hardware is
// through hal/*. This is the template for every future app: drivers
// and services provide the features, the app composes them.
//
// F0 chose GP10 as the STATUS LED (the v2.x PCB's actual one; GP25 is
// not connected). The choice is still hard-coded here; later phases
// promote it to a board descriptor the drivers share.

#include "hal/gpio.h"
#include "hal/time.h"

#define STATUS_LED_PIN 10

int main(void) {
    hal_gpio_init(STATUS_LED_PIN, HAL_GPIO_DIR_OUT);

    while (true) {
        hal_gpio_put(STATUS_LED_PIN, true);
        hal_sleep_ms(500);
        hal_gpio_put(STATUS_LED_PIN, false);
        hal_sleep_ms(500);
    }
}

// FaultyCat v3 — firmware entrypoint.
//
// F0 scope: blink the onboard LED on GPIO25 so we can prove the pipeline
// (submodules → cmake → cross-compile → UF2 → BOOTSEL flash → blinking board)
// end-to-end on real hardware. Every following phase replaces this file
// with a more substantive main:
//
//   F1 — same blink, but via the HAL wrapper we write.
//   F2 — driver diag menu over UART.
//   F3 — USB composite with 4× CDC, first byte echoed.
//   …
//
// GPIO25 is the onboard status LED on the RP2040 Pico footprint used by
// FaultyCat v2.x. Confirmed against `firmware/c/board_config.h`
// (`PIN_LED_STATUS = 25`) and the v2.x schematic.

#include "pico/stdlib.h"

#define STATUS_LED_PIN 25

int main(void) {
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);

    while (true) {
        gpio_put(STATUS_LED_PIN, 1);
        sleep_ms(500);
        gpio_put(STATUS_LED_PIN, 0);
        sleep_ms(500);
    }
}

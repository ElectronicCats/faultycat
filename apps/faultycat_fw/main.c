// FaultyCat v3 — firmware entrypoint.
//
// F0 scope: blink the STATUS LED on GP10 so we can prove the pipeline
// (submodules → cmake → cross-compile → UF2 → BOOTSEL flash → blinking
// board) end-to-end on real hardware. Every following phase replaces
// this file with a more substantive main:
//
//   F1 — same blink, but via the HAL wrapper we write.
//   F2 — driver diag menu over UART.
//   F3 — USB composite with 4× CDC, first byte echoed.
//   …
//
// Pin choice: FaultyCat v2.x mounts the RP2040 **directly** (no Pico
// module), so GP25 (Pico's "onboard LED") is NOT connected on this
// board. The legacy firmware/c/board_config.h's `PIN_LED_STATUS = 25`
// is a Pico-module relic that never applied to the v2.x PCB. The
// real STATUS LED is wired to GP10 — confirmed by the maintainer on
// 2026-04-23 against a physical v2.2 board.

#include "pico/stdlib.h"

#define STATUS_LED_PIN 10

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

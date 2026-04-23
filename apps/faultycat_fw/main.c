// FaultyCat v3 — firmware entrypoint.
//
// F2a commit 2 introduces ui_buttons and a USB-serial diag loop so
// the maintainer can verify the button wiring by watching events on
// /dev/ttyACM0. The STATUS LED also mirrors "either button pressed"
// as a pure-optical fallback if the serial is unavailable.
//
// TEMPORARY — `pico_enable_stdio_usb(1)` uses the pico-sdk default
// single-CDC descriptor. F3 replaces this with the real composite
// descriptor (4× CDC + vendor + HID), at which point the diag moves
// to the scanner CDC and the VID:PID switches to 1209:FA17. Treat
// this file as a scaffold that will be re-written once the USB
// layer lands.

#include <stdio.h>

#include "pico/stdlib.h"   // for stdio_init_all — scoped to F2 diag, see above

#include "hal/time.h"
#include "ui_buttons.h"
#include "ui_leds.h"

#define POLL_PERIOD_MS 20u

static void print_button_transition(const char *name, bool pressed) {
    printf("%s %s\n", name, pressed ? "PRESSED" : "RELEASED");
}

int main(void) {
    // pico-sdk USB stdio: re-enabled from F0/F1's off state specifically
    // for the F2 diag loop. Pre-F3 scaffolding only.
    stdio_init_all();

    ui_leds_init();
    ui_buttons_init();

    bool last_arm   = false;
    bool last_pulse = false;

    printf("\nFaultyCat v3 — F2a diag (ui_buttons)\n");
    printf("Press ARM and PULSE; each transition prints here.\n");
    printf("STATUS LED lights while either is pressed.\n\n");

    while (true) {
        bool arm   = ui_buttons_is_pressed(UI_BTN_ARM);
        bool pulse = ui_buttons_is_pressed(UI_BTN_PULSE);

        ui_leds_set(UI_LED_STATUS, arm || pulse);

        if (arm != last_arm) {
            print_button_transition("ARM", arm);
            last_arm = arm;
        }
        if (pulse != last_pulse) {
            print_button_transition("PULSE", pulse);
            last_pulse = pulse;
        }

        hal_sleep_ms(POLL_PERIOD_MS);
    }
}

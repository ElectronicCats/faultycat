// FaultyCat v3 — firmware entrypoint.
//
// F2a commit 3 extends the diag loop with target_monitor: GP29 ADC
// readings print every 500 ms so the maintainer can verify the analog
// input is alive and sane.
//
// TEMPORARY stdio_usb setup (carries over from F2a-2). F3 replaces
// it with the proper USB composite descriptor.

#include <stdio.h>

#include "pico/stdlib.h"

#include "hal/time.h"
#include "target_monitor.h"
#include "ui_buttons.h"
#include "ui_leds.h"

#define BUTTON_POLL_PERIOD_MS 20u
#define ADC_PRINT_PERIOD_MS   500u

static void print_button_transition(const char *name, bool pressed) {
    printf("%s %s\n", name, pressed ? "PRESSED" : "RELEASED");
}

int main(void) {
    stdio_init_all();

    ui_leds_init();
    ui_buttons_init();
    target_monitor_init();

    bool     last_arm           = false;
    bool     last_pulse         = false;
    uint32_t last_adc_print_ms  = 0;

    printf("\nFaultyCat v3 — F2a diag (buttons + target_monitor)\n");
    printf("Buttons: events on change. Target ADC (GP29): raw 12-bit every %u ms.\n\n",
           ADC_PRINT_PERIOD_MS);

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

        uint32_t now = hal_now_ms();
        if ((now - last_adc_print_ms) >= ADC_PRINT_PERIOD_MS) {
            uint16_t raw = target_monitor_read_raw();
            printf("ADC raw=%4u (%.2f %% of 3.3V)\n",
                   raw, (float)raw * 100.0f / 4095.0f);
            last_adc_print_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

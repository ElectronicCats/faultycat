// FaultyCat v3 — firmware entrypoint.
//
// F2a commit 5 (last of F2a) adds ext_trigger on GP8. Snapshot now
// also shows the trigger level. Default pull is DOWN so a floating
// trigger input reads 0 — same default the legacy firmware used for
// the "pull none" case wouldn't have survived open-wire noise.
//
// TEMPORARY stdio_usb (moves to the composite scanner CDC in F3).

#include <stdio.h>

#include "pico/stdlib.h"

#include "ext_trigger.h"
#include "hal/time.h"
#include "scanner_io.h"
#include "target_monitor.h"
#include "ui_buttons.h"
#include "ui_leds.h"

#define BUTTON_POLL_PERIOD_MS 20u
#define SNAPSHOT_PERIOD_MS    500u

static void print_button_transition(const char *name, bool pressed) {
    printf("%s %s\n", name, pressed ? "PRESSED" : "RELEASED");
}

static void print_snapshot(void) {
    uint16_t adc     = target_monitor_read_raw();
    uint8_t  scan    = scanner_io_read_all();
    bool     trigger = ext_trigger_level();

    char bits[SCANNER_IO_CHANNEL_COUNT + 1];
    for (unsigned i = 0; i < SCANNER_IO_CHANNEL_COUNT; i++) {
        bits[i] = (scan & (1u << (SCANNER_IO_CHANNEL_COUNT - 1 - i))) ? '1' : '0';
    }
    bits[SCANNER_IO_CHANNEL_COUNT] = '\0';

    printf("ADC=%4u (%5.2f%%)  SCAN[CH7..CH0]=%s (0x%02X)  TRIG=%d\n",
           adc, (float)adc * 100.0f / 4095.0f, bits, scan, trigger ? 1 : 0);
}

int main(void) {
    stdio_init_all();

    ui_leds_init();
    ui_buttons_init();
    target_monitor_init();
    scanner_io_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);

    bool     last_arm          = false;
    bool     last_pulse        = false;
    uint32_t last_snapshot_ms  = 0;

    printf("\nFaultyCat v3 — F2a diag (all low-risk drivers)\n");
    printf("SCAN (GP0..GP7) = pullup input. TRIG (GP8) = pulldown input.\n");
    printf("Ground / drive pins to watch the bits flip.\n\n");

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
        if ((now - last_snapshot_ms) >= SNAPSHOT_PERIOD_MS) {
            print_snapshot();
            last_snapshot_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

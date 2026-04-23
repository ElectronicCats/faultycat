// FaultyCat v3 — firmware entrypoint.
//
// F2b commit 1 adds crowbar_mosfet. The diag auto-cycles through
// NONE → LP → HP → NONE every 2 seconds so the maintainer can put
// a multimeter on GP16 and GP17 (or on the MOSFET-side test points)
// and see each gate come up in turn.
//
// SAFETY — this driver does NOT apply HV. It only conmuta gate
// GPIOs. With the HV charger disarmed (as it is in F2), nothing
// discharges. Valid to run this with the plastic shield off; valid
// to probe the board manually.

#include <stdio.h>

#include "pico/stdlib.h"

#include "crowbar_mosfet.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "scanner_io.h"
#include "target_monitor.h"
#include "ui_buttons.h"
#include "ui_leds.h"

#define BUTTON_POLL_PERIOD_MS   20u
#define SNAPSHOT_PERIOD_MS      500u
#define CROWBAR_CYCLE_PERIOD_MS 2000u

static void print_button_transition(const char *name, bool pressed) {
    printf("%s %s\n", name, pressed ? "PRESSED" : "RELEASED");
}

static const char *crowbar_label(crowbar_path_t p) {
    switch (p) {
        case CROWBAR_PATH_NONE: return "NONE";
        case CROWBAR_PATH_LP:   return "LP  ";
        case CROWBAR_PATH_HP:   return "HP  ";
        default:                return "???";
    }
}

static void print_snapshot(void) {
    uint16_t       adc     = target_monitor_read_raw();
    uint8_t        scan    = scanner_io_read_all();
    bool           trigger = ext_trigger_level();
    crowbar_path_t path    = crowbar_mosfet_get_path();

    char bits[SCANNER_IO_CHANNEL_COUNT + 1];
    for (unsigned i = 0; i < SCANNER_IO_CHANNEL_COUNT; i++) {
        bits[i] = (scan & (1u << (SCANNER_IO_CHANNEL_COUNT - 1 - i))) ? '1' : '0';
    }
    bits[SCANNER_IO_CHANNEL_COUNT] = '\0';

    printf("ADC=%4u (%5.2f%%)  SCAN[CH7..CH0]=%s  TRIG=%d  CROWBAR=%s\n",
           adc, (float)adc * 100.0f / 4095.0f, bits, trigger ? 1 : 0,
           crowbar_label(path));
}

static crowbar_path_t next_crowbar(crowbar_path_t cur) {
    switch (cur) {
        case CROWBAR_PATH_NONE: return CROWBAR_PATH_LP;
        case CROWBAR_PATH_LP:   return CROWBAR_PATH_HP;
        case CROWBAR_PATH_HP:   return CROWBAR_PATH_NONE;
        default:                return CROWBAR_PATH_NONE;
    }
}

int main(void) {
    stdio_init_all();

    ui_leds_init();
    ui_buttons_init();
    target_monitor_init();
    scanner_io_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    crowbar_mosfet_init();

    bool     last_arm              = false;
    bool     last_pulse            = false;
    uint32_t last_snapshot_ms      = 0;
    uint32_t last_crowbar_step_ms  = 0;

    printf("\nFaultyCat v3 — F2b-1 diag (crowbar_mosfet added)\n");
    printf("CROWBAR auto-cycles NONE -> LP -> HP every 2s.\n");
    printf("Put multimeter on GP16 (LP) and GP17 (HP) to see them come up in turn.\n");
    printf("No HV is applied — MOSFET gates only.\n\n");

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

        if ((now - last_crowbar_step_ms) >= CROWBAR_CYCLE_PERIOD_MS) {
            crowbar_path_t cur  = crowbar_mosfet_get_path();
            crowbar_path_t next = next_crowbar(cur);
            crowbar_mosfet_set_path(next);
            printf("CROWBAR -> %s\n", crowbar_label(next));
            last_crowbar_step_ms = now;
        }

        if ((now - last_snapshot_ms) >= SNAPSHOT_PERIOD_MS) {
            print_snapshot();
            last_snapshot_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

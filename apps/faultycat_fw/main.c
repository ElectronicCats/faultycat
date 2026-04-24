// FaultyCat v3 — firmware entrypoint.
//
// F3-1: the pico-sdk stdio_usb default is replaced by our own
// composite USB device exposing 4× CDC (emfi, crowbar, scanner,
// target-uart). This commit does NOT migrate the HV diag onto the
// composite — that lands in F3-4. For now:
//
//   * Every CDC echoes whatever you type into it (enumeration +
//     bidirectional traffic check).
//   * ARM / PULSE buttons still control the HV charger and the EMFI
//     fire path, so safety behaviour is unchanged.
//   * LEDs mirror HV + status as before.
//   * Crowbar LP/HP auto-cycle still runs.
//   * Serial diag is SILENT until F3-4. Validate via `lsusb -v` and
//     `picocom /dev/ttyACMn` for echo per CDC.

#include "crowbar_mosfet.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"
#include "scanner_io.h"
#include "target_monitor.h"
#include "ui_buttons.h"
#include "ui_leds.h"
#include "usb_composite.h"

#define BUTTON_POLL_PERIOD_MS    20u
#define CROWBAR_CYCLE_PERIOD_MS  2000u
#define EMFI_MANUAL_WIDTH_US     5u

static crowbar_path_t next_crowbar(crowbar_path_t cur) {
    switch (cur) {
        case CROWBAR_PATH_NONE: return CROWBAR_PATH_LP;
        case CROWBAR_PATH_LP:   return CROWBAR_PATH_HP;
        case CROWBAR_PATH_HP:   return CROWBAR_PATH_NONE;
        default:                return CROWBAR_PATH_NONE;
    }
}

static void try_fire_emfi(void) {
    if (!hv_charger_is_armed())   return;
    if (!hv_charger_is_charged()) return;
    if (!emfi_pulse_fire_manual(EMFI_MANUAL_WIDTH_US)) return;
    hv_charger_disarm();
}

int main(void) {
    usb_composite_init();

    ui_leds_init();
    ui_buttons_init();
    target_monitor_init();
    scanner_io_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    crowbar_mosfet_init();
    hv_charger_init();
    emfi_pulse_init();

    bool     last_arm              = false;
    bool     last_pulse            = false;
    uint32_t last_crowbar_step_ms  = 0;

    while (true) {
        usb_composite_task();

        bool arm   = ui_buttons_is_pressed(UI_BTN_ARM);
        bool pulse = ui_buttons_is_pressed(UI_BTN_PULSE);

        if (arm && !last_arm) {
            if (hv_charger_is_armed()) {
                hv_charger_disarm();
            } else {
                hv_charger_arm();
            }
        }
        if (pulse && !last_pulse) {
            try_fire_emfi();
        }

        last_arm   = arm;
        last_pulse = pulse;

        hv_charger_tick();

        ui_leds_set(UI_LED_CHARGE_ON, hv_charger_is_armed());
        ui_leds_hv_detected_feed(hv_charger_is_charged());
        ui_leds_set(UI_LED_STATUS, arm || pulse);

        uint32_t now = hal_now_ms();
        if ((now - last_crowbar_step_ms) >= CROWBAR_CYCLE_PERIOD_MS) {
            crowbar_path_t cur  = crowbar_mosfet_get_path();
            crowbar_path_t next = next_crowbar(cur);
            crowbar_mosfet_set_path(next);
            last_crowbar_step_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

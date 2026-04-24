// FaultyCat v3 — firmware entrypoint.
//
// F2b commit 3 brings up drivers/hv_charger.
//
// *** HV DIAG RULES ***
//   * Boot state is DISARMED. The charger only runs when the user
//     presses ARM.
//   * ARM button → hv_charger_arm(). CHARGE_ON LED lights.
//   * PULSE button → hv_charger_disarm(). LED turns off.
//   * 60-second auto-disarm is enforced every loop iteration by
//     hv_charger_tick().
//   * HV_DETECTED LED follows the CHARGED feedback via the hysteresis
//     helper in drivers/ui_leds (500 ms hold).
//
// SAFETY: by design this file NEVER arms the HV charger without an
// explicit human button press. Do not add auto-arm logic to the diag.

#include <stdio.h>

#include "pico/stdlib.h"

#include "crowbar_mosfet.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"
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
    bool           armed   = hv_charger_is_armed();
    bool           charged = hv_charger_is_charged();

    char bits[SCANNER_IO_CHANNEL_COUNT + 1];
    for (unsigned i = 0; i < SCANNER_IO_CHANNEL_COUNT; i++) {
        bits[i] = (scan & (1u << (SCANNER_IO_CHANNEL_COUNT - 1 - i))) ? '1' : '0';
    }
    bits[SCANNER_IO_CHANNEL_COUNT] = '\0';

    printf("ADC=%4u  SCAN=%s  TRIG=%d  CROWBAR=%s  HV[%s%s]\n",
           adc, bits, trigger ? 1 : 0, crowbar_label(path),
           armed   ? "ARM" : "---",
           charged ? " CHG" : "");
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
    hv_charger_init();              // DISARMED

    bool     last_arm              = false;
    bool     last_pulse            = false;
    uint32_t last_snapshot_ms      = 0;
    uint32_t last_crowbar_step_ms  = 0;

    printf("\n========================================\n");
    printf("FaultyCat v3 — F2b-3 diag (HV CHARGER ADDED)\n");
    printf("========================================\n");
    printf("!! HV WARNING — plastic shield MUST be installed.\n");
    printf("!! Do NOT leave the SMA open with HV armed.\n");
    printf("\n");
    printf(" ARM button   : arm HV charger (60s auto-disarm)\n");
    printf(" PULSE button : disarm HV charger\n");
    printf(" CROWBAR auto-cycles every 2s (no HV in this path yet — F5).\n");
    printf(" Snapshot every %u ms.\n\n", SNAPSHOT_PERIOD_MS);

    while (true) {
        bool arm   = ui_buttons_is_pressed(UI_BTN_ARM);
        bool pulse = ui_buttons_is_pressed(UI_BTN_PULSE);

        // Edge detect so a held button doesn't spam arm/disarm calls.
        if (arm && !last_arm) {
            printf("USER: ARM pressed — arming HV charger.\n");
            hv_charger_arm();
        } else if (!arm && last_arm) {
            print_button_transition("ARM", false);
        }

        if (pulse && !last_pulse) {
            printf("USER: PULSE pressed — disarming HV charger.\n");
            hv_charger_disarm();
        } else if (!pulse && last_pulse) {
            print_button_transition("PULSE", false);
        }

        last_arm   = arm;
        last_pulse = pulse;

        // Enforce 60-second auto-disarm.
        hv_charger_tick();

        // Drive the LEDs straight from driver state.
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

        if ((now - last_snapshot_ms) >= SNAPSHOT_PERIOD_MS) {
            print_snapshot();
            last_snapshot_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

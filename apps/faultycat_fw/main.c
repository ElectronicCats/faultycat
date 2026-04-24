// FaultyCat v3 — firmware entrypoint.
//
// F2b commit 4 wires in drivers/emfi_pulse. The diag mirrors the
// legacy UX:
//   * ARM button (edge): toggle HV charger on/off.
//   * PULSE button (edge): FIRE EMFI pulse — only if armed AND
//     charged. After firing, auto-disarm for safety.
//
// The pulse path is CPU-timed (portado del legacy picoemp_pulse):
// interrupts disabled, GP14 high, busy-wait, low, interrupts
// restored, 250 ms cool-down. PIO-driven triggered pulses arrive in
// F4 (glitch_engine service).
//
// SAFETY: never auto-fires. Always requires explicit PULSE button
// press. Fire is rejected if the charger isn't armed+charged.

#include <stdio.h>

#include "pico/stdlib.h"

#include "crowbar_mosfet.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"
#include "scanner_io.h"
#include "target_monitor.h"
#include "ui_buttons.h"
#include "ui_leds.h"

#define BUTTON_POLL_PERIOD_MS    20u
#define SNAPSHOT_PERIOD_MS       500u
#define CROWBAR_CYCLE_PERIOD_MS  2000u
#define EMFI_MANUAL_WIDTH_US     5u

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

static void try_fire_emfi(void) {
    bool armed   = hv_charger_is_armed();
    bool charged = hv_charger_is_charged();

    if (!armed) {
        printf("EMFI FIRE REJECTED: charger not ARMED\n");
        return;
    }
    if (!charged) {
        printf("EMFI FIRE REJECTED: cap not CHARGED yet\n");
        return;
    }

    printf("EMFI FIRE: width=%uus\n", EMFI_MANUAL_WIDTH_US);
    bool ok = emfi_pulse_fire_manual(EMFI_MANUAL_WIDTH_US);
    if (!ok) {
        printf("EMFI FIRE: width rejected by driver\n");
        return;
    }

    // Safety: auto-disarm after every fire. Cap is largely spent
    // anyway, but enforce so the operator is not left with an armed
    // charger accumulating again.
    hv_charger_disarm();
    printf("EMFI FIRE: done, HV auto-disarmed\n");
}

int main(void) {
    stdio_init_all();

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
    uint32_t last_snapshot_ms      = 0;
    uint32_t last_crowbar_step_ms  = 0;

    printf("\n========================================\n");
    printf("FaultyCat v3 — F2b-4 diag (EMFI PULSE ADDED)\n");
    printf("========================================\n");
    printf("!! HV WARNING — plastic shield + coil MUST be installed.\n");
    printf("!! Do NOT leave the SMA open with HV armed.\n");
    printf("\n");
    printf(" ARM button   : toggle HV charger on/off (60s auto-disarm)\n");
    printf(" PULSE button : FIRE EMFI pulse (%u us) — requires armed+charged\n",
           EMFI_MANUAL_WIDTH_US);
    printf("                auto-disarms after fire\n");
    printf(" CROWBAR auto-cycles every 2s (no HV in this path).\n\n");

    while (true) {
        bool arm   = ui_buttons_is_pressed(UI_BTN_ARM);
        bool pulse = ui_buttons_is_pressed(UI_BTN_PULSE);

        // ARM edge → toggle charger
        if (arm && !last_arm) {
            if (hv_charger_is_armed()) {
                printf("USER: ARM pressed — disarming.\n");
                hv_charger_disarm();
            } else {
                printf("USER: ARM pressed — arming (60s auto-disarm).\n");
                hv_charger_arm();
            }
        }

        // PULSE edge → fire EMFI if safe
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

        if ((now - last_snapshot_ms) >= SNAPSHOT_PERIOD_MS) {
            print_snapshot();
            last_snapshot_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

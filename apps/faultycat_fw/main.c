// FaultyCat v3 — firmware entrypoint.
//
// F3-4: diagnostic stream migrates from the pico-sdk stdio_usb default
// (single-CDC) to CDC2 "Scanner Shell" on our composite. The banner,
// button events, HV state, crowbar transitions, and periodic snapshot
// line all appear there now — connect with `picocom /dev/ttyACM<N>`
// where N is whichever of our four CDCs is the third (iInterface
// "FaultyCat Scanner Shell").
//
// Output is gated on `usb_composite_cdc_connected(USB_CDC_SCANNER)`
// so we don't accumulate TX bytes nobody's reading. First connection
// prints the banner so you don't have to reset the device to see it.

#include <stdarg.h>
#include <stdio.h>

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
#define SNAPSHOT_PERIOD_MS       500u
#define CROWBAR_CYCLE_PERIOD_MS  2000u
#define EMFI_MANUAL_WIDTH_US     5u

// -----------------------------------------------------------------------------
// Diag log — vsnprintf into a stack buffer, shove into CDC2. Non-blocking;
// drops on a full TX FIFO, which is the right behaviour for diagnostics.
// -----------------------------------------------------------------------------

static void diag_printf(const char *fmt, ...) {
    if (!usb_composite_cdc_connected(USB_CDC_SCANNER)) {
        return;
    }
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    usb_composite_cdc_write(USB_CDC_SCANNER, buf, (size_t)n);
}

static void diag_banner(void) {
    diag_printf("\n========================================\n");
    diag_printf("FaultyCat v3 — F3 diag (composite scanner CDC)\n");
    diag_printf("========================================\n");
    diag_printf("!! HV WARNING — plastic shield + coil MUST be installed.\n");
    diag_printf("!! Do NOT leave the SMA open with HV armed.\n\n");
    diag_printf(" ARM button   : toggle HV charger on/off (60s auto-disarm)\n");
    diag_printf(" PULSE button : FIRE EMFI pulse (%u us) — armed+charged only\n",
                EMFI_MANUAL_WIDTH_US);
    diag_printf("                auto-disarms after fire\n");
    diag_printf(" CROWBAR auto-cycles every 2s (no HV in this path yet)\n");
    diag_printf(" Snapshot every %u ms.\n\n", SNAPSHOT_PERIOD_MS);
}

// -----------------------------------------------------------------------------
// Snapshot + helpers
// -----------------------------------------------------------------------------

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

    diag_printf("ADC=%4u  SCAN=%s  TRIG=%d  CROWBAR=%s  HV[%s%s]\n",
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
    if (!hv_charger_is_armed()) {
        diag_printf("EMFI FIRE REJECTED: charger not ARMED\n");
        return;
    }
    if (!hv_charger_is_charged()) {
        diag_printf("EMFI FIRE REJECTED: cap not CHARGED yet\n");
        return;
    }
    diag_printf("EMFI FIRE: width=%u us\n", EMFI_MANUAL_WIDTH_US);
    if (!emfi_pulse_fire_manual(EMFI_MANUAL_WIDTH_US)) {
        diag_printf("EMFI FIRE: width rejected by driver\n");
        return;
    }
    hv_charger_disarm();
    diag_printf("EMFI FIRE: done, HV auto-disarmed\n");
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------

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
    bool     last_scanner_conn     = false;
    uint32_t last_snapshot_ms      = 0;
    uint32_t last_crowbar_step_ms  = 0;

    while (true) {
        usb_composite_task();

        // Print banner on first CDC2 connection so a freshly-attached
        // terminal sees the intro without needing a board reset.
        bool conn = usb_composite_cdc_connected(USB_CDC_SCANNER);
        if (conn && !last_scanner_conn) {
            diag_banner();
        }
        last_scanner_conn = conn;

        bool arm   = ui_buttons_is_pressed(UI_BTN_ARM);
        bool pulse = ui_buttons_is_pressed(UI_BTN_PULSE);

        if (arm && !last_arm) {
            if (hv_charger_is_armed()) {
                diag_printf("USER: ARM pressed — disarming.\n");
                hv_charger_disarm();
            } else {
                diag_printf("USER: ARM pressed — arming (60s auto-disarm).\n");
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
            diag_printf("CROWBAR -> %s\n", crowbar_label(next));
            last_crowbar_step_ms = now;
        }

        if ((now - last_snapshot_ms) >= SNAPSHOT_PERIOD_MS) {
            print_snapshot();
            last_snapshot_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

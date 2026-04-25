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

#include "crowbar_campaign.h"
#include "crowbar_mosfet.h"
#include "crowbar_proto.h"
#include "emfi_campaign.h"
#include "emfi_capture.h"
#include "emfi_proto.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"
#include "board_v2.h"
#include "scanner_io.h"
#include "swd_dp.h"
#include "swd_mem.h"
#include "swd_phy.h"
#include "target_monitor.h"
#include "ui_buttons.h"
#include "ui_leds.h"
#include "usb_composite.h"

#include <stdlib.h>
#include <string.h>

#define BUTTON_POLL_PERIOD_MS    20u
#define SNAPSHOT_PERIOD_MS       500u
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
    diag_printf("FaultyCat v3 — F5 diag (composite scanner CDC)\n");
    diag_printf("========================================\n");
    diag_printf("!! HV WARNING — plastic shield + coil MUST be installed.\n");
    diag_printf("!! Do NOT leave the SMA open with HV armed.\n\n");
    diag_printf(" ARM button   : toggle HV charger on/off (60s auto-disarm)\n");
    diag_printf(" PULSE button : FIRE EMFI pulse (%u us) — armed+charged only\n",
                EMFI_MANUAL_WIDTH_US);
    diag_printf("                auto-disarms after fire\n");
    diag_printf(" CROWBAR      : controlled via CDC1 (crowbar_proto)\n");
    diag_printf("                — `tools/crowbar_client.py ping` to verify\n");
    diag_printf(" EMFI         : controlled via CDC0 (emfi_proto)\n");
    diag_printf(" SWD          : line-buffered shell on this CDC (CDC2)\n");
    diag_printf("                type `?` for the command list\n");
    diag_printf(" Snapshot every %u ms.\n\n", SNAPSHOT_PERIOD_MS);
}

// -----------------------------------------------------------------------------
// SWD diagnostic shell on CDC2 (F6-5)
//
// Tiny line-buffered text parser. Lets the operator drive the F6
// services from a serial terminal or from tools/swd_diag.py without
// needing a host-side CMSIS-DAP stack (that lands in F7). The shell
// shares CDC2 with the diag snapshot stream — outputs are prefixed
// "SWD: " so they stay distinguishable.
// -----------------------------------------------------------------------------

#define SWD_SHELL_BUF_LEN  96u

static char     swd_shell_buf[SWD_SHELL_BUF_LEN];
static uint16_t swd_shell_pos = 0u;
static bool     swd_shell_inited = false;

static void swd_print(const char *s) {
    if (!s) return;
    usb_composite_cdc_write(USB_CDC_SCANNER, s, strlen(s));
}

static void swd_printf(const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    usb_composite_cdc_write(USB_CDC_SCANNER, buf, (size_t)n);
}

static void swd_shell_help(void) {
    swd_print("SWD: commands —\n");
    swd_print("SWD:   ? | help\n");
    swd_print("SWD:   swd init <swclk_gp> <swdio_gp> [<nrst_gp>]   defaults 0 1 2\n");
    swd_print("SWD:   swd deinit\n");
    swd_print("SWD:   swd freq <khz>                               (100..24000)\n");
    swd_print("SWD:   swd connect | swd probe                      line reset + DPIDR\n");
    swd_print("SWD:   swd read32 <hex_addr>\n");
    swd_print("SWD:   swd write32 <hex_addr> <hex_val>\n");
    swd_print("SWD:   swd reset 0|1                                nRST release / assert\n");
}

static const char *ack_label(swd_dp_ack_t a) {
    switch (a) {
        case SWD_ACK_OK:           return "OK";
        case SWD_ACK_WAIT:         return "WAIT";
        case SWD_ACK_FAULT:        return "FAULT";
        case SWD_ACK_PARITY_ERR:   return "PARITY_ERR";
        case SWD_ACK_NO_TARGET:    return "NO_TARGET";
    }
    return "UNKNOWN";
}

// Lazy-init: any swd_* command that needs the phy auto-inits with
// the scanner-header defaults if the operator hasn't called
// `swd init` yet. Saves typing in the common case.
static bool ensure_inited(void) {
    if (swd_shell_inited) return true;
    if (!swd_phy_init(BOARD_GP_SWCLK_DEFAULT,
                      BOARD_GP_SWDIO_DEFAULT,
                      BOARD_GP_SWRST_DEFAULT)) {
        swd_print("SWD: ERR phy_init_failed\n");
        return false;
    }
    swd_shell_inited = true;
    return true;
}

static void cmd_init(int argc, char **argv) {
    if (swd_shell_inited) {
        swd_phy_deinit();
        swd_shell_inited = false;
    }
    uint8_t swclk = (argc >= 3) ? (uint8_t)strtoul(argv[2], NULL, 0)
                                : BOARD_GP_SWCLK_DEFAULT;
    uint8_t swdio = (argc >= 4) ? (uint8_t)strtoul(argv[3], NULL, 0)
                                : BOARD_GP_SWDIO_DEFAULT;
    int8_t  nrst  = (argc >= 5) ? (int8_t)strtol(argv[4], NULL, 0)
                                : (int8_t)BOARD_GP_SWRST_DEFAULT;
    if (!swd_phy_init(swclk, swdio, nrst)) {
        swd_print("SWD: ERR phy_init_failed\n");
        return;
    }
    swd_shell_inited = true;
    swd_printf("SWD: OK init swclk=GP%u swdio=GP%u nrst=%d\n",
               swclk, swdio, nrst);
}

static void cmd_deinit(void) {
    if (!swd_shell_inited) {
        swd_print("SWD: OK deinit (was already idle)\n");
        return;
    }
    swd_phy_deinit();
    swd_shell_inited = false;
    swd_print("SWD: OK deinit\n");
}

static void cmd_freq(int argc, char **argv) {
    if (argc < 3) {
        swd_print("SWD: ERR missing_khz\n");
        return;
    }
    if (!ensure_inited()) return;
    uint32_t khz = (uint32_t)strtoul(argv[2], NULL, 0);
    swd_phy_set_clk_khz(khz);
    swd_printf("SWD: OK freq %u khz (clamped to range if needed)\n", khz);
}

static void cmd_connect(void) {
    if (!ensure_inited()) return;
    uint32_t dpidr = 0u;
    swd_dp_ack_t ack = swd_dp_connect(&dpidr);
    if (ack == SWD_ACK_OK) {
        swd_printf("SWD: OK connect dpidr=0x%08lX\n", (unsigned long)dpidr);
    } else {
        swd_printf("SWD: ERR connect ack=%s\n", ack_label(ack));
    }
}

static void cmd_read32(int argc, char **argv) {
    if (argc < 3) {
        swd_print("SWD: ERR missing_addr\n");
        return;
    }
    if (!ensure_inited()) return;
    uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 16);
    swd_dp_ack_t ack = swd_mem_init();
    if (ack != SWD_ACK_OK) {
        swd_printf("SWD: ERR mem_init ack=%s\n", ack_label(ack));
        return;
    }
    uint32_t val = 0u;
    ack = swd_mem_read32(addr, &val);
    if (ack == SWD_ACK_OK) {
        swd_printf("SWD: OK read32 [0x%08lX]=0x%08lX\n",
                   (unsigned long)addr, (unsigned long)val);
    } else {
        swd_printf("SWD: ERR read32 ack=%s\n", ack_label(ack));
    }
}

static void cmd_write32(int argc, char **argv) {
    if (argc < 4) {
        swd_print("SWD: ERR missing_addr_or_val\n");
        return;
    }
    if (!ensure_inited()) return;
    uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 16);
    uint32_t val  = (uint32_t)strtoul(argv[3], NULL, 16);
    swd_dp_ack_t ack = swd_mem_init();
    if (ack != SWD_ACK_OK) {
        swd_printf("SWD: ERR mem_init ack=%s\n", ack_label(ack));
        return;
    }
    ack = swd_mem_write32(addr, val);
    if (ack == SWD_ACK_OK) {
        swd_printf("SWD: OK write32 [0x%08lX]<=0x%08lX\n",
                   (unsigned long)addr, (unsigned long)val);
    } else {
        swd_printf("SWD: ERR write32 ack=%s\n", ack_label(ack));
    }
}

static void cmd_reset(int argc, char **argv) {
    if (argc < 3) {
        swd_print("SWD: ERR missing_state\n");
        return;
    }
    if (!ensure_inited()) return;
    bool assert_low = (argv[2][0] == '1');
    swd_phy_assert_reset(assert_low);
    swd_printf("SWD: OK reset asserted=%d level=%d\n",
               assert_low ? 1 : 0, swd_phy_reset_level());
}

static void process_swd_line(char *line) {
    // Tokenize on whitespace; up to 5 tokens (cmd + 4 args).
    char *argv[5];
    int   argc = 0;
    char *save;
    char *tok = strtok_r(line, " \t", &save);
    while (tok && argc < 5) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
    if (argc == 0) return;

    if (!strcmp(argv[0], "?") || !strcmp(argv[0], "help")) {
        swd_shell_help();
        return;
    }
    if (strcmp(argv[0], "swd") != 0) {
        swd_printf("SWD: ERR unknown_cmd: %s (try `?`)\n", argv[0]);
        return;
    }
    if (argc < 2) {
        swd_print("SWD: ERR swd needs subcommand (try `?`)\n");
        return;
    }
    const char *sub = argv[1];
    if      (!strcmp(sub, "init"))    cmd_init(argc, argv);
    else if (!strcmp(sub, "deinit"))  cmd_deinit();
    else if (!strcmp(sub, "freq"))    cmd_freq(argc, argv);
    else if (!strcmp(sub, "connect")
          || !strcmp(sub, "probe"))   cmd_connect();
    else if (!strcmp(sub, "read32"))  cmd_read32(argc, argv);
    else if (!strcmp(sub, "write32")) cmd_write32(argc, argv);
    else if (!strcmp(sub, "reset"))   cmd_reset(argc, argv);
    else {
        swd_printf("SWD: ERR unknown_subcmd: %s (try `?`)\n", sub);
    }
}

static void pump_swd_shell_cdc(void) {
    uint8_t buf[64];
    size_t n = usb_composite_cdc_read(USB_CDC_SCANNER, buf, sizeof(buf));
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = buf[i];
        if (b == '\r' || b == '\n') {
            if (swd_shell_pos > 0u) {
                swd_shell_buf[swd_shell_pos] = '\0';
                usb_composite_cdc_write(USB_CDC_SCANNER, "\n", 1);
                process_swd_line(swd_shell_buf);
                swd_shell_pos = 0u;
            } else if (b == '\n') {
                // Bare newline — quietly ignore so paired \r\n from a
                // terminal doesn't emit an empty-line error.
            }
        } else if (b == 0x7Fu || b == 0x08u) {   // backspace / DEL
            if (swd_shell_pos > 0u) {
                swd_shell_pos--;
                usb_composite_cdc_write(USB_CDC_SCANNER, "\b \b", 3);
            }
        } else if (b >= 0x20u && b < 0x7Fu) {
            if (swd_shell_pos + 1u < SWD_SHELL_BUF_LEN) {
                swd_shell_buf[swd_shell_pos++] = (char)b;
                usb_composite_cdc_write(USB_CDC_SCANNER, &b, 1);
            }
        }
        // else: ignore non-printable
    }
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
    // Skip the single-shot ADC read while emfi_capture owns the FIFO.
    // adc_read() blocks waiting for CS_READY when the ADC is in
    // continuous/FIFO/DMA mode — would wedge this loop.
    uint16_t       adc     = emfi_capture_is_running() ? 0u
                                                       : target_monitor_read_raw();
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

    emfi_status_t es; emfi_campaign_get_status(&es);
    static const char *emfi_labels[] = {
        "IDLE","ARMING","CHARGED","WAITING","FIRED","ERROR"
    };
    const char *elabel = (es.state < 6) ? emfi_labels[es.state] : "???";

    crowbar_status_t cs; crowbar_campaign_get_status(&cs);
    static const char *crowbar_labels[] = {
        "IDLE","ARMING","ARMED","WAITING","FIRED","ERROR"
    };
    const char *clabel = (cs.state < 6) ? crowbar_labels[cs.state] : "???";

    diag_printf("ADC=%4u SCAN=%s TRIG=%d GATE=%s HV[%s%s] EMFI=%s CROW=%s\n",
                adc, bits, trigger ? 1 : 0, crowbar_label(path),
                armed   ? "ARM" : "---",
                charged ? " CHG" : "",
                elabel, clabel);
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
// host_proto pumps — CDC0 = emfi, CDC1 = crowbar
// -----------------------------------------------------------------------------

// Pump CDC0 bytes through emfi_proto. Writes any reply to CDC0
// without blocking.
static void pump_emfi_cdc(void) {
    uint8_t buf[64];
    // We reuse tinyusb's read interface via usb_composite; a small
    // internal shim keeps usb_composite as the only owner of
    // tud_cdc_n_read.
    size_t n = usb_composite_cdc_read(USB_CDC_EMFI, buf, sizeof(buf));
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        if (emfi_proto_feed(buf[i], hal_now_ms())) {
            uint8_t reply[768];
            size_t rn = emfi_proto_dispatch(reply, sizeof(reply));
            if (rn > 0) {
                usb_composite_cdc_write(USB_CDC_EMFI, reply, rn);
            }
        }
    }
}

// Pump CDC1 bytes through crowbar_proto. Replies max out at ~21 bytes
// (STATUS) so a small stack buffer is plenty.
static void pump_crowbar_cdc(void) {
    uint8_t buf[64];
    size_t n = usb_composite_cdc_read(USB_CDC_CROWBAR, buf, sizeof(buf));
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        if (crowbar_proto_feed(buf[i], hal_now_ms())) {
            uint8_t reply[64];
            size_t rn = crowbar_proto_dispatch(reply, sizeof(reply));
            if (rn > 0) {
                usb_composite_cdc_write(USB_CDC_CROWBAR, reply, rn);
            }
        }
    }
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
    emfi_campaign_init();
    emfi_proto_init();
    crowbar_campaign_init();
    crowbar_proto_init();

    bool     last_arm              = false;
    bool     last_pulse            = false;
    bool     last_scanner_conn     = false;
    uint32_t last_snapshot_ms      = 0;

    while (true) {
        usb_composite_task();

        pump_emfi_cdc();
        pump_crowbar_cdc();
        pump_swd_shell_cdc();
        emfi_campaign_tick();
        crowbar_campaign_tick();

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

        // Defense-in-depth: emfi_campaign_tick() also calls this, but
        // the button-ARM path (F2b) does not go through the service,
        // so keep the direct call so the 60s auto-disarm invariant
        // holds regardless of service state.
        hv_charger_tick();

        ui_leds_set(UI_LED_CHARGE_ON, hv_charger_is_armed());
        ui_leds_hv_detected_feed(hv_charger_is_charged());
        ui_leds_set(UI_LED_STATUS, arm || pulse);

        uint32_t now = hal_now_ms();

        if ((now - last_snapshot_ms) >= SNAPSHOT_PERIOD_MS) {
            print_snapshot();
            last_snapshot_ms = now;
        }

        hal_sleep_ms(BUTTON_POLL_PERIOD_MS);
    }
}

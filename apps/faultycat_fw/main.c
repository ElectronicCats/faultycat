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
#include "buspirate_compat.h"
#include "ext_trigger.h"
#include "flashrom_serprog.h"
#include "hal/gpio.h"
#include "hal/time.h"
#include "hv_charger.h"
#include "jtag_core.h"
#include "board_v2.h"
#include "pinout_scanner.h"
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

// Shell input modes — F8-3 introduced the dispatcher, F8-4 plugs in
// the BusPirate binary parser, F8-5 will plug in serprog. While in a
// binary mode we route every CDC2 byte through the corresponding
// service's feed_byte() and we GAG the diag snapshot stream so it
// doesn't shred the binary protocol. Declared this high in the file
// so diag_printf (just below) can see it.
typedef enum {
    SHELL_MODE_TEXT       = 0,
    SHELL_MODE_BUSPIRATE  = 1,
    SHELL_MODE_SERPROG    = 2,
} shell_mode_t;

static shell_mode_t s_shell_mode = SHELL_MODE_TEXT;

// -----------------------------------------------------------------------------
// Diag log — vsnprintf into a stack buffer, shove into CDC2. Non-blocking;
// drops on a full TX FIFO, which is the right behaviour for diagnostics.
// -----------------------------------------------------------------------------

static void diag_printf(const char *fmt, ...) {
    if (!usb_composite_cdc_connected(USB_CDC_SCANNER)) {
        return;
    }
    // F8-4: while CDC2 is in a binary mode (BusPirate / serprog) the
    // shell stream is owned by the foreign protocol — emitting diag
    // text would corrupt OpenOCD / flashrom traffic. Gag.
    if (s_shell_mode != SHELL_MODE_TEXT) {
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
    diag_printf("FaultyCat v3 — F8 diag (composite scanner CDC + unified shell)\n");
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
    diag_printf(" JTAG         : `jtag <subcmd>` on this CDC (CDC2)\n");
    diag_printf("                type `?` for the command list\n");
    diag_printf(" Snapshot every %u ms.\n\n", SNAPSHOT_PERIOD_MS);
}

// -----------------------------------------------------------------------------
// Top-level diagnostic shell on CDC2 (F6-5 → F8-3)
//
// Tiny line-buffered text parser shared by all the v3 debug services.
// Lets the operator drive everything from a serial terminal or from
// tools/{swd,jtag,scanner}_diag.py without needing a host-side
// CMSIS-DAP stack (that lands in F7). The shell shares CDC2 with the
// diag snapshot stream — outputs use a service prefix so the
// host-side filters can demux:
//
//   SHELL:    top-level menu / help / unknown-cmd errors
//   SWD:      F6 SWD subshell (`swd <subcmd>`)
//   JTAG:     F8-1 JTAG subshell (`jtag <subcmd>`)
//   SCAN:     F8-2 pinout scanner (`scan jtag` / `scan swd`)
//   BPIRATE:  F8-4 BusPirate binary mode entry/exit (placeholder
//             until F8-4 lands the real protocol parser).
//   SERPROG:  F8-5 flashrom serprog mode entry/exit (placeholder).
//
// Mode switches (`buspirate enter`, `serprog enter`) currently emit
// a "not_yet_implemented" error so the parser shape is stable for
// F8-4 / F8-5 to plug in without further surgery on this file.
// -----------------------------------------------------------------------------

#define SHELL_BUF_LEN  96u

static char     shell_buf[SHELL_BUF_LEN];
static uint16_t shell_pos = 0u;
static bool     swd_shell_inited = false;

static void shell_print(const char *s) {
    if (!s) return;
    usb_composite_cdc_write(USB_CDC_SCANNER, s, strlen(s));
}

static void shell_printf(const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1;
    usb_composite_cdc_write(USB_CDC_SCANNER, buf, (size_t)n);
}

static void shell_help(void) {
    shell_print("SHELL: commands —\n");
    shell_print("SHELL:   ? | help\n");
    shell_print("SHELL: --- SWD (F6) ---\n");
    shell_print("SHELL:   swd init <swclk_gp> <swdio_gp> [<nrst_gp>]   defaults 0 1 2\n");
    shell_print("SHELL:   swd deinit\n");
    shell_print("SHELL:   swd freq <khz>                               (100..24000)\n");
    shell_print("SHELL:   swd connect | swd probe                      line reset + DPIDR\n");
    shell_print("SHELL:   swd read32 <hex_addr>\n");
    shell_print("SHELL:   swd write32 <hex_addr> <hex_val>\n");
    shell_print("SHELL:   swd reset 0|1                                nRST release / assert\n");
    shell_print("SHELL: --- JTAG (F8-1) ---\n");
    shell_print("SHELL:   jtag init <tdi_gp> <tdo_gp> <tms_gp> <tck_gp> [<trst_gp>]\n");
    shell_print("SHELL:   jtag deinit\n");
    shell_print("SHELL:   jtag reset                                   TAP → Run-Test/Idle\n");
    shell_print("SHELL:   jtag trst                                    pulse TRST low ~1 ms\n");
    shell_print("SHELL:   jtag chain                                   detect # of TAPs\n");
    shell_print("SHELL:   jtag idcode                                  read IDCODE chain\n");
    shell_print("SHELL: --- Pinout scan (F8-2) ---\n");
    shell_print("SHELL:   scan jtag                                    P(8,4)=1680 perms\n");
    shell_print("SHELL:   scan swd  [<targetsel_hex>]                  P(8,2)=56 perms\n");
    shell_print("SHELL: --- Mode switches (binary protocols) ---\n");
    shell_print("SHELL:   buspirate enter [<tdi> <tdo> <tms> <tck>]    OpenOCD via BPv1 binary (F8-4)\n");
    shell_print("SHELL:                                                defaults: 0 1 2 3, exit with 0x0F\n");
    shell_print("SHELL:   serprog enter [<cs> <mosi> <miso> <sck>]     flashrom serprog (F8-5)\n");
    shell_print("SHELL:                                                defaults: 0 1 2 3, exit on host disconnect\n");
    shell_print("SHELL: NOTE: SWD and JTAG share scanner pins (GP0..GP7) — only one\n");
    shell_print("SHELL:       may be inited at a time. F9 lifts this to a real mutex.\n");
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
        shell_print("SWD: ERR phy_init_failed\n");
        return false;
    }
    swd_shell_inited = true;
    return true;
}

static void cmd_init(int argc, char **argv) {
    // F8-1 soft-lock: SWD and JTAG share GP0..GP7. Refuse SWD init
    // while JTAG owns the bus instead of silently corrupting both.
    if (jtag_is_inited()) {
        shell_print("SWD: ERR jtag_in_use (run `jtag deinit` first)\n");
        return;
    }
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
        shell_print("SWD: ERR phy_init_failed\n");
        return;
    }
    swd_shell_inited = true;
    shell_printf("SWD: OK init swclk=GP%u swdio=GP%u nrst=%d\n",
               swclk, swdio, nrst);
}

static void cmd_deinit(void) {
    if (!swd_shell_inited) {
        shell_print("SWD: OK deinit (was already idle)\n");
        return;
    }
    swd_phy_deinit();
    swd_shell_inited = false;
    shell_print("SWD: OK deinit\n");
}

static void cmd_freq(int argc, char **argv) {
    if (argc < 3) {
        shell_print("SWD: ERR missing_khz\n");
        return;
    }
    if (!ensure_inited()) return;
    uint32_t khz = (uint32_t)strtoul(argv[2], NULL, 0);
    swd_phy_set_clk_khz(khz);
    shell_printf("SWD: OK freq %u khz (clamped to range if needed)\n", khz);
}

static void cmd_connect(void) {
    if (!ensure_inited()) return;
    uint32_t dpidr = 0u;
    // Default to RP2040 core 0 — F8 will let the operator choose
    // a different TARGETID via the shell once jtag_core lands.
    swd_dp_ack_t ack = swd_dp_connect(SWD_DP_TARGETSEL_RP2040_CORE0, &dpidr);
    if (ack == SWD_ACK_OK) {
        shell_printf("SWD: OK connect dpidr=0x%08lX\n", (unsigned long)dpidr);
    } else {
        shell_printf("SWD: ERR connect ack=%s\n", ack_label(ack));
    }
}

static void cmd_read32(int argc, char **argv) {
    if (argc < 3) {
        shell_print("SWD: ERR missing_addr\n");
        return;
    }
    if (!ensure_inited()) return;
    uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 16);
    swd_dp_ack_t ack = swd_mem_init();
    if (ack != SWD_ACK_OK) {
        shell_printf("SWD: ERR mem_init ack=%s\n", ack_label(ack));
        return;
    }
    uint32_t val = 0u;
    ack = swd_mem_read32(addr, &val);
    if (ack == SWD_ACK_OK) {
        shell_printf("SWD: OK read32 [0x%08lX]=0x%08lX\n",
                   (unsigned long)addr, (unsigned long)val);
    } else {
        shell_printf("SWD: ERR read32 ack=%s\n", ack_label(ack));
    }
}

static void cmd_write32(int argc, char **argv) {
    if (argc < 4) {
        shell_print("SWD: ERR missing_addr_or_val\n");
        return;
    }
    if (!ensure_inited()) return;
    uint32_t addr = (uint32_t)strtoul(argv[2], NULL, 16);
    uint32_t val  = (uint32_t)strtoul(argv[3], NULL, 16);
    swd_dp_ack_t ack = swd_mem_init();
    if (ack != SWD_ACK_OK) {
        shell_printf("SWD: ERR mem_init ack=%s\n", ack_label(ack));
        return;
    }
    ack = swd_mem_write32(addr, val);
    if (ack == SWD_ACK_OK) {
        shell_printf("SWD: OK write32 [0x%08lX]<=0x%08lX\n",
                   (unsigned long)addr, (unsigned long)val);
    } else {
        shell_printf("SWD: ERR write32 ack=%s\n", ack_label(ack));
    }
}

static void cmd_reset(int argc, char **argv) {
    if (argc < 3) {
        shell_print("SWD: ERR missing_state\n");
        return;
    }
    if (!ensure_inited()) return;
    bool assert_low = (argv[2][0] == '1');
    swd_phy_assert_reset(assert_low);
    shell_printf("SWD: OK reset asserted=%d level=%d\n",
               assert_low ? 1 : 0, swd_phy_reset_level());
}

// -----------------------------------------------------------------------------
// F8-1 JTAG sub-shell — `jtag <subcmd>`
//
// Same dispatcher style as the SWD section above. Output prefix is
// "JTAG:" so a host-side parser (tools/jtag_diag.py, F8-G1) can demux
// SWD vs JTAG replies on the shared CDC2 stream.
// -----------------------------------------------------------------------------

static void cmd_jtag_init(int argc, char **argv) {
    if (swd_shell_inited) {
        shell_print("JTAG: ERR swd_in_use (run `swd deinit` first)\n");
        return;
    }
    if (argc < 6) {
        shell_print("JTAG: ERR usage: jtag init <tdi> <tdo> <tms> <tck> [<trst>]\n");
        return;
    }
    if (jtag_is_inited()) jtag_deinit();
    jtag_pinout_t p = {
        .tdi  = (uint8_t)strtoul(argv[2], NULL, 0),
        .tdo  = (uint8_t)strtoul(argv[3], NULL, 0),
        .tms  = (uint8_t)strtoul(argv[4], NULL, 0),
        .tck  = (uint8_t)strtoul(argv[5], NULL, 0),
        .trst = (argc >= 7) ? (int8_t)strtol(argv[6], NULL, 0)
                            : (int8_t)JTAG_PIN_TRST_NONE,
    };
    if (!jtag_init(&p)) {
        shell_print("JTAG: ERR init_failed (pin range or duplicate?)\n");
        return;
    }
    shell_printf("JTAG: OK init tdi=GP%u tdo=GP%u tms=GP%u tck=GP%u trst=%d\n",
               p.tdi, p.tdo, p.tms, p.tck, p.trst);
}

static void cmd_jtag_deinit(void) {
    if (!jtag_is_inited()) {
        shell_print("JTAG: OK deinit (was already idle)\n");
        return;
    }
    jtag_deinit();
    shell_print("JTAG: OK deinit\n");
}

static void cmd_jtag_reset(void) {
    if (!jtag_is_inited()) {
        shell_print("JTAG: ERR not_inited (run `jtag init ...` first)\n");
        return;
    }
    jtag_reset_to_run_test_idle();
    shell_print("JTAG: OK reset (TAP → Run-Test/Idle)\n");
}

static void cmd_jtag_trst(void) {
    if (!jtag_is_inited()) {
        shell_print("JTAG: ERR not_inited\n");
        return;
    }
    jtag_assert_trst();   // no-op if no TRST wired
    shell_print("JTAG: OK trst pulse (no-op if no TRST)\n");
}

static void cmd_jtag_chain(void) {
    if (!jtag_is_inited()) {
        shell_print("JTAG: ERR not_inited\n");
        return;
    }
    size_t n = jtag_detect_chain_length();
    shell_printf("JTAG: OK chain devices=%u\n", (unsigned)n);
}

static void cmd_jtag_idcode(void) {
    if (!jtag_is_inited()) {
        shell_print("JTAG: ERR not_inited\n");
        return;
    }
    uint32_t ids[JTAG_MAX_DEVICES];
    size_t n = jtag_read_idcodes(ids, JTAG_MAX_DEVICES);
    if (n == 0u) {
        shell_print("JTAG: ERR no_target (chain length 0)\n");
        return;
    }
    shell_printf("JTAG: OK idcodes count=%u\n", (unsigned)n);
    for (size_t i = 0; i < n; i++) {
        bool valid = jtag_idcode_is_valid(ids[i]);
        uint32_t bank = (ids[i] >> 8) & 0xFu;
        uint32_t mfg  = (ids[i] >> 1) & 0x7Fu;
        uint32_t part = (ids[i] >> 12) & 0xFFFFu;
        uint32_t ver  = (ids[i] >> 28) & 0xFu;
        shell_printf("JTAG:   [%u] 0x%08lX %s mfg_bank=0x%X mfg_id=0x%02X "
                   "part=0x%04X ver=0x%X\n",
                   (unsigned)i, (unsigned long)ids[i],
                   valid ? "VALID" : "INVALID",
                   (unsigned)bank, (unsigned)mfg,
                   (unsigned)part, (unsigned)ver);
    }
}

// -----------------------------------------------------------------------------
// F8-2 pinout scanner sub-shell — `scan jtag` / `scan swd`
//
// Both scans iterate hundreds of permutations; each iteration calls
// usb_composite_task + the EMFI/crowbar pumps via scan_yield_progress
// so a long scan doesn't starve TinyUSB or stall an active campaign.
// Progress is printed on CDC2 every 100 iterations.
// -----------------------------------------------------------------------------

// Forward decls of the per-CDC pumps so scan_yield_progress can call
// them without dragging the function above the SWD shell block.
static void pump_emfi_cdc(void);
static void pump_crowbar_cdc(void);

static uint32_t s_scan_last_progress_print = 0u;

static void scan_yield_progress(uint32_t cur, uint32_t total) {
    // Keep TinyUSB and the campaigns alive between candidates so the
    // host sees CDC enumeration + can interrupt; honour the memory
    // rule on never starving tud_task during a long blocking op.
    usb_composite_task();
    pump_emfi_cdc();
    pump_crowbar_cdc();
    emfi_campaign_tick();
    crowbar_campaign_tick();

    // Print progress every 100 iterations. The 0-th iteration always
    // prints so the operator sees the scan started. Reset the
    // throttle counter at scan-start (cur=0) so a back-to-back
    // `scan jtag` then `scan swd` doesn't race the 100-step throttle.
    if (cur == 0u) s_scan_last_progress_print = 0u;
    if (cur == 0u || (cur - s_scan_last_progress_print) >= 100u) {
        shell_printf("SCAN: progress %lu/%lu\n",
                   (unsigned long)cur, (unsigned long)total);
        s_scan_last_progress_print = cur;
    }
}

static void cmd_scan_jtag(void) {
    if (jtag_is_inited()) {
        shell_print("SCAN: ERR jtag_in_use (run `jtag deinit` first)\n");
        return;
    }
    if (swd_shell_inited) {
        shell_print("SCAN: ERR swd_in_use (run `swd deinit` first)\n");
        return;
    }
    shell_printf("SCAN: starting JTAG pinout scan over %u channels (P(%u,%u)=%lu)\n",
               PINOUT_SCANNER_CHANNELS, PINOUT_SCANNER_CHANNELS,
               PINOUT_SCANNER_JTAG_PINS,
               (unsigned long)PINOUT_SCANNER_JTAG_TOTAL);
    pinout_scan_jtag_result_t r;
    bool found = pinout_scan_jtag(&r, scan_yield_progress);
    if (!found) {
        shell_print("SCAN: jtag NO_MATCH (no valid IDCODE found)\n");
        return;
    }
    shell_printf("SCAN: jtag MATCH tdi=GP%u tdo=GP%u tms=GP%u tck=GP%u\n",
               r.tdi, r.tdo, r.tms, r.tck);
    shell_printf("SCAN:   chain=%u idcode[0]=0x%08lX\n",
               (unsigned)r.chain_length, (unsigned long)r.idcode);
}

static void cmd_scan_swd(int argc, char **argv) {
    if (jtag_is_inited()) {
        shell_print("SCAN: ERR jtag_in_use (run `jtag deinit` first)\n");
        return;
    }
    if (swd_shell_inited) {
        shell_print("SCAN: ERR swd_in_use (run `swd deinit` first)\n");
        return;
    }
    uint32_t targetsel = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 16)
                                     : SWD_DP_TARGETSEL_RP2040_CORE0;
    shell_printf("SCAN: starting SWD pinout scan over %u channels "
               "(P(%u,%u)=%u) targetsel=0x%08lX\n",
               PINOUT_SCANNER_CHANNELS, PINOUT_SCANNER_CHANNELS,
               PINOUT_SCANNER_SWD_PINS, PINOUT_SCANNER_SWD_TOTAL,
               (unsigned long)targetsel);
    pinout_scan_swd_result_t r;
    bool found = pinout_scan_swd(targetsel, &r, scan_yield_progress);
    if (!found) {
        shell_print("SCAN: swd NO_MATCH (no OK DPIDR found)\n");
        return;
    }
    shell_printf("SCAN: swd MATCH swclk=GP%u swdio=GP%u\n", r.swclk, r.swdio);
    shell_printf("SCAN:   dpidr=0x%08lX targetsel=0x%08lX\n",
               (unsigned long)r.dpidr, (unsigned long)r.targetsel);
}

static void process_scan_subcmd(int argc, char **argv) {
    if (argc < 2) {
        shell_print("SCAN: ERR scan needs subcommand: jtag | swd\n");
        return;
    }
    const char *sub = argv[1];
    if      (!strcmp(sub, "jtag")) cmd_scan_jtag();
    else if (!strcmp(sub, "swd"))  cmd_scan_swd(argc, argv);
    else {
        shell_printf("SCAN: ERR unknown_subcmd: %s (try `?`)\n", sub);
    }
}

// -----------------------------------------------------------------------------
// F8-3 mode-switch placeholders for `buspirate enter` (F8-4) and
// `serprog enter` (F8-5).
//
// These emit a stable error shape today so:
//   1. The parser dispatch table is in place — F8-4 / F8-5 just
//      replace the placeholder with the real binary-mode pump.
//   2. tools/jtag_diag.py and friends can probe whether the firmware
//      already supports a given mode by sending the `enter` command
//      and inspecting the prefix.
//   3. The help text doesn't lie — operators see the planned modes
//      and a clear "not yet" status.
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// F8-4 BusPirate compat — bridge callbacks + entry command
// -----------------------------------------------------------------------------

static void bp_write_byte_cb(uint8_t b, void *u) {
    (void)u;
    usb_composite_cdc_write(USB_CDC_SCANNER, &b, 1);
}

static bool bp_jtag_clock_bit_cb(bool tms, bool tdi, void *u) {
    (void)u;
    return jtag_clock_bit(tms, tdi);
}

static void bp_on_exit_cb(void *u) {
    (void)u;
    if (jtag_is_inited()) jtag_deinit();
    s_shell_mode = SHELL_MODE_TEXT;
    shell_print("\nBPIRATE: OK exited (back to text shell)\n");
}

static const buspirate_compat_callbacks_t BP_CALLBACKS = {
    .write_byte     = bp_write_byte_cb,
    .jtag_clock_bit = bp_jtag_clock_bit_cb,
    .on_exit        = bp_on_exit_cb,
    .user           = NULL,
};

static void process_buspirate_subcmd(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "enter") != 0) {
        shell_print("BPIRATE: ERR usage: buspirate enter [<tdi> <tdo> <tms> <tck>]\n");
        return;
    }
    if (jtag_is_inited()) {
        shell_print("BPIRATE: ERR jtag_in_use (run `jtag deinit` first)\n");
        return;
    }
    if (swd_shell_inited) {
        shell_print("BPIRATE: ERR swd_in_use (run `swd deinit` first)\n");
        return;
    }
    bool explicit_pins = (argc >= 6);
    jtag_pinout_t p = {
        .tdi  = explicit_pins ? (uint8_t)strtoul(argv[2], NULL, 0)
                              : BOARD_GP_SCANNER_CH0,
        .tdo  = explicit_pins ? (uint8_t)strtoul(argv[3], NULL, 0)
                              : BOARD_GP_SCANNER_CH1,
        .tms  = explicit_pins ? (uint8_t)strtoul(argv[4], NULL, 0)
                              : BOARD_GP_SCANNER_CH2,
        .tck  = explicit_pins ? (uint8_t)strtoul(argv[5], NULL, 0)
                              : BOARD_GP_SCANNER_CH3,
        .trst = JTAG_PIN_TRST_NONE,
    };
    if (!jtag_init(&p)) {
        shell_print("BPIRATE: ERR jtag_init_failed (pin range or duplicate?)\n");
        return;
    }
    buspirate_compat_init(&BP_CALLBACKS);
    shell_printf("BPIRATE: OK entering BBIO mode tdi=GP%u tdo=GP%u tms=GP%u tck=GP%u\n",
                 p.tdi, p.tdo, p.tms, p.tck);
    shell_print("BPIRATE: send 0x00 to handshake (BBIO1), 0x06 → OCD1, 0x0F to exit\n");
    // Set mode AFTER the prints so the diag-gate doesn't swallow them.
    s_shell_mode = SHELL_MODE_BUSPIRATE;
}

// -----------------------------------------------------------------------------
// F8-5 flashrom_serprog — bridge callbacks + SPI bit-bang + entry command
// -----------------------------------------------------------------------------

// Serprog SPI pinout — set at session start in `serprog enter`. The
// scanner header is GP0..GP7; defaults assign CS/MOSI/MISO/SCK to
// CH0..CH3 so the operator can leave the rest free for trigger
// monitoring during a flash dump.
static uint8_t s_sp_pin_cs   = BOARD_GP_SCANNER_CH0;
static uint8_t s_sp_pin_mosi = BOARD_GP_SCANNER_CH1;
static uint8_t s_sp_pin_miso = BOARD_GP_SCANNER_CH2;
static uint8_t s_sp_pin_sck  = BOARD_GP_SCANNER_CH3;
static bool    s_sp_pins_owned = false;

static void sp_write_byte_cb(uint8_t b, void *u) {
    (void)u;
    usb_composite_cdc_write(USB_CDC_SCANNER, &b, 1);
}

// SPI mode 0 (CPOL=0, CPHA=0), MSB-first per 25-series flash
// convention. Drive MOSI then pulse SCK low→high (target latches
// MOSI, presents next MISO bit) → sample MISO → high→low.
static uint8_t sp_xfer_byte_cb(uint8_t out, void *u) {
    (void)u;
    uint8_t in = 0u;
    for (int bit = 7; bit >= 0; bit--) {
        hal_gpio_put(s_sp_pin_mosi, (bool)((out >> bit) & 1u));
        hal_gpio_put(s_sp_pin_sck,  true);
        if (hal_gpio_get(s_sp_pin_miso)) in |= (uint8_t)(1u << bit);
        hal_gpio_put(s_sp_pin_sck,  false);
    }
    return in;
}

static void sp_cs_set_cb(bool low, void *u) {
    (void)u;
    // CS is active-low. `low=true` means "assert" → drive low.
    hal_gpio_put(s_sp_pin_cs, !low);
}

static void sp_yield_cb(void *u) {
    (void)u;
    // Same cooperative-tasking shape as the F8-2 scan_yield_progress.
    usb_composite_task();
    pump_emfi_cdc();
    pump_crowbar_cdc();
    emfi_campaign_tick();
    crowbar_campaign_tick();
}

static void sp_release_pins(void) {
    if (!s_sp_pins_owned) return;
    hal_gpio_init(s_sp_pin_cs,   HAL_GPIO_DIR_IN);
    hal_gpio_init(s_sp_pin_mosi, HAL_GPIO_DIR_IN);
    hal_gpio_init(s_sp_pin_miso, HAL_GPIO_DIR_IN);
    hal_gpio_init(s_sp_pin_sck,  HAL_GPIO_DIR_IN);
    hal_gpio_set_pulls(s_sp_pin_cs,   false, false);
    hal_gpio_set_pulls(s_sp_pin_mosi, false, false);
    hal_gpio_set_pulls(s_sp_pin_miso, false, false);
    hal_gpio_set_pulls(s_sp_pin_sck,  false, false);
    s_sp_pins_owned = false;
}

static void sp_on_exit_cb(void *u) {
    (void)u;
    sp_release_pins();
    s_shell_mode = SHELL_MODE_TEXT;
    shell_print("\nSERPROG: OK exited (back to text shell)\n");
}

static const flashrom_serprog_callbacks_t SP_CALLBACKS = {
    .write_byte    = sp_write_byte_cb,
    .spi_cs_set    = sp_cs_set_cb,
    .spi_xfer_byte = sp_xfer_byte_cb,
    .yield         = sp_yield_cb,
    .on_exit       = sp_on_exit_cb,
    .user          = NULL,
};

static void process_serprog_subcmd(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "enter") != 0) {
        shell_print("SERPROG: ERR usage: serprog enter [<cs> <mosi> <miso> <sck>]\n");
        return;
    }
    if (jtag_is_inited()) {
        shell_print("SERPROG: ERR jtag_in_use (run `jtag deinit` first)\n");
        return;
    }
    if (swd_shell_inited) {
        shell_print("SERPROG: ERR swd_in_use (run `swd deinit` first)\n");
        return;
    }
    bool explicit_pins = (argc >= 6);
    s_sp_pin_cs   = explicit_pins ? (uint8_t)strtoul(argv[2], NULL, 0)
                                  : BOARD_GP_SCANNER_CH0;
    s_sp_pin_mosi = explicit_pins ? (uint8_t)strtoul(argv[3], NULL, 0)
                                  : BOARD_GP_SCANNER_CH1;
    s_sp_pin_miso = explicit_pins ? (uint8_t)strtoul(argv[4], NULL, 0)
                                  : BOARD_GP_SCANNER_CH2;
    s_sp_pin_sck  = explicit_pins ? (uint8_t)strtoul(argv[5], NULL, 0)
                                  : BOARD_GP_SCANNER_CH3;

    // Drive idle states. CS high (deasserted), MOSI low, SCK low,
    // MISO input + pull-up (so a floating bus reads as 0xFF, the
    // 25-series no-chip-attached signature).
    hal_gpio_init(s_sp_pin_cs,   HAL_GPIO_DIR_OUT);
    hal_gpio_init(s_sp_pin_mosi, HAL_GPIO_DIR_OUT);
    hal_gpio_init(s_sp_pin_sck,  HAL_GPIO_DIR_OUT);
    hal_gpio_init(s_sp_pin_miso, HAL_GPIO_DIR_IN);
    hal_gpio_set_pulls(s_sp_pin_miso, true, false);
    hal_gpio_put(s_sp_pin_cs,   true);
    hal_gpio_put(s_sp_pin_mosi, false);
    hal_gpio_put(s_sp_pin_sck,  false);
    s_sp_pins_owned = true;

    flashrom_serprog_init(&SP_CALLBACKS);
    shell_printf("SERPROG: OK entering serprog mode cs=GP%u mosi=GP%u miso=GP%u sck=GP%u\n",
                 s_sp_pin_cs, s_sp_pin_mosi, s_sp_pin_miso, s_sp_pin_sck);
    shell_print("SERPROG: ready for `flashrom -p serprog:dev=/dev/ttyACM<N>`\n");
    shell_print("SERPROG: exit by closing the host port (DTR drop is detected)\n");
    s_shell_mode = SHELL_MODE_SERPROG;
}

static void process_jtag_subcmd(int argc, char **argv) {
    if (argc < 2) {
        shell_print("JTAG: ERR jtag needs subcommand (try `?`)\n");
        return;
    }
    const char *sub = argv[1];
    if      (!strcmp(sub, "init"))    cmd_jtag_init(argc, argv);
    else if (!strcmp(sub, "deinit"))  cmd_jtag_deinit();
    else if (!strcmp(sub, "reset"))   cmd_jtag_reset();
    else if (!strcmp(sub, "trst"))    cmd_jtag_trst();
    else if (!strcmp(sub, "chain"))   cmd_jtag_chain();
    else if (!strcmp(sub, "idcode")
          || !strcmp(sub, "idcodes")) cmd_jtag_idcode();
    else {
        shell_printf("JTAG: ERR unknown_subcmd: %s (try `?`)\n", sub);
    }
}

static void process_shell_line(char *line) {
    // Tokenize on whitespace; up to 8 tokens — `jtag init <tdi> <tdo>
    // <tms> <tck> <trst>` is the longest at 7 tokens.
    char *argv[8];
    int   argc = 0;
    char *save;
    char *tok = strtok_r(line, " \t", &save);
    while (tok && argc < 8) {
        argv[argc++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
    if (argc == 0) return;

    if (!strcmp(argv[0], "?") || !strcmp(argv[0], "help")) {
        shell_help();
        return;
    }
    if (!strcmp(argv[0], "jtag")) {
        process_jtag_subcmd(argc, argv);
        return;
    }
    if (!strcmp(argv[0], "scan")) {
        process_scan_subcmd(argc, argv);
        return;
    }
    if (!strcmp(argv[0], "buspirate")) {
        process_buspirate_subcmd(argc, argv);
        return;
    }
    if (!strcmp(argv[0], "serprog")) {
        process_serprog_subcmd(argc, argv);
        return;
    }
    if (strcmp(argv[0], "swd") != 0) {
        shell_printf("SHELL: ERR unknown_cmd: %s (try `?`)\n", argv[0]);
        return;
    }
    if (argc < 2) {
        shell_print("SWD: ERR swd needs subcommand (try `?`)\n");
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
        shell_printf("SWD: ERR unknown_subcmd: %s (try `?`)\n", sub);
    }
}

static void pump_shell_cdc(void) {
    uint8_t buf[64];
    size_t n = usb_composite_cdc_read(USB_CDC_SCANNER, buf, sizeof(buf));
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        uint8_t b = buf[i];

        // F8-4 / F8-5: in a binary mode every byte goes through the
        // foreign protocol parser. The mode-switch back to TEXT is
        // owned by the parser (e.g. BusPirate's 0x0F invokes
        // bp_on_exit_cb which clears s_shell_mode).
        if (s_shell_mode == SHELL_MODE_BUSPIRATE) {
            buspirate_compat_feed_byte(b);
            continue;
        }
        if (s_shell_mode == SHELL_MODE_SERPROG) {
            flashrom_serprog_feed_byte(b);
            continue;
        }

        if (b == '\r' || b == '\n') {
            if (shell_pos > 0u) {
                shell_buf[shell_pos] = '\0';
                usb_composite_cdc_write(USB_CDC_SCANNER, "\n", 1);
                process_shell_line(shell_buf);
                shell_pos = 0u;
            } else if (b == '\n') {
                // Bare newline — quietly ignore so paired \r\n from a
                // terminal doesn't emit an empty-line error.
            }
        } else if (b == 0x7Fu || b == 0x08u) {   // backspace / DEL
            if (shell_pos > 0u) {
                shell_pos--;
                usb_composite_cdc_write(USB_CDC_SCANNER, "\b \b", 3);
            }
        } else if (b >= 0x20u && b < 0x7Fu) {
            if (shell_pos + 1u < SHELL_BUF_LEN) {
                shell_buf[shell_pos++] = (char)b;
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
        pump_shell_cdc();
        emfi_campaign_tick();
        crowbar_campaign_tick();

        // Print banner on first CDC2 connection so a freshly-attached
        // terminal sees the intro without needing a board reset.
        bool conn = usb_composite_cdc_connected(USB_CDC_SCANNER);
        if (conn && !last_scanner_conn) {
            diag_banner();
        }
        // F8-4 / F8-5: host-side disconnect (DTR drop) while we're
        // mid-binary-mode → tear down whichever foreign protocol owns
        // the shell so the next session starts clean. BusPirate has
        // its own 0x0F escape but a crashed OpenOCD won't send it;
        // serprog has no protocol exit at all and depends on this.
        if (last_scanner_conn && !conn) {
            if (s_shell_mode == SHELL_MODE_BUSPIRATE) {
                bp_on_exit_cb(NULL);
            } else if (s_shell_mode == SHELL_MODE_SERPROG) {
                sp_on_exit_cb(NULL);
            }
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

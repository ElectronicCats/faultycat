#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// services/pinout_scanner — JTAGulator-style brute-force pinout
// discovery over the FaultyCat v2.x scanner header (8 channels,
// GP0..GP7).
//
// Algorithm derived from blueTag's `jtagScan` and `swdScan`
// (third_party/blueTag @ v2.1.2, MIT — attribution at file head of
// pinout_scanner.c). Reimplemented against `services/jtag_core` and
// `services/swd_core` so the v3 layered model holds — no direct
// pico-sdk gpio reach-around.
//
// What "scanning" means:
//   - JTAG : iterate every 4-tuple (TDI, TDO, TMS, TCK) of distinct
//            scanner channels (P(8, 4) = 1680 permutations); for each,
//            jtag_init + jtag_read_idcodes; first valid IDCODE wins.
//   - SWD  : iterate every 2-tuple (SWCLK, SWDIO) of distinct scanner
//            channels (P(8, 2) = 56 permutations); for each, swd_phy
//            + swd_dp_connect; first OK ACK with non-zero DPIDR wins.
//
// Mutual-exclusion contract (F8-1 → F9). The scan acquires the
// scanner pins by calling jtag_init / swd_phy_init in sequence.
// Each candidate iteration calls deinit before the next init so
// the soft-lock is re-checkable. The shell-level soft-lock that
// F8-1 added between SWD and JTAG still applies — `scan` refuses
// if either swd_phy or jtag_core is currently held.

#define PINOUT_SCANNER_CHANNELS  8u
#define PINOUT_SCANNER_JTAG_PINS 4u
#define PINOUT_SCANNER_SWD_PINS  2u

// Total permutation counts the scan iterates through.
//   P(8, 4) = 1680     P(8, 2) = 56
#define PINOUT_SCANNER_JTAG_TOTAL 1680u
#define PINOUT_SCANNER_SWD_TOTAL  56u

typedef struct {
    uint8_t  tdi;
    uint8_t  tdo;
    uint8_t  tms;
    uint8_t  tck;
    uint32_t idcode;          // first device's IDCODE on the matched chain
    size_t   chain_length;
} pinout_scan_jtag_result_t;

typedef struct {
    uint8_t  swclk;
    uint8_t  swdio;
    uint32_t dpidr;
    uint32_t targetsel;       // TARGETSEL the match was found under
} pinout_scan_swd_result_t;

// Progress / yield callback signature. Called once per iteration
// before the candidate is tested. `cur` is the 0-based index of the
// candidate about to run, `total` is the iteration upper bound. The
// callback may call cooperative tasks (usb_composite_task, glitch
// engine ticks) so a long scan doesn't starve them. Pass NULL to
// disable.
typedef void (*pinout_scanner_progress_cb)(uint32_t cur, uint32_t total);

// Run a JTAG pinout scan. Returns true on first valid match
// (chain_length ≥ 1 AND first IDCODE passes jtag_idcode_is_valid).
// On false, no permutation matched. Either way the function returns
// with both jtag_core and swd_phy in the deinit state.
bool pinout_scan_jtag(pinout_scan_jtag_result_t *out,
                      pinout_scanner_progress_cb cb);

// Run an SWD pinout scan against the supplied TARGETSEL. Returns
// true on first OK DPIDR read. The shell exposes one entry point
// that walks the RP2040 TARGETSEL (CORE0); other multidrop IDs can
// be tried by passing them explicitly.
bool pinout_scan_swd(uint32_t targetsel,
                     pinout_scan_swd_result_t *out,
                     pinout_scanner_progress_cb cb);

// -----------------------------------------------------------------------------
// Pure permutation iterator — exposed for tests.
//
// Generates k-permutations of [0..n-1] in lexicographic order.
// `k` must be in 1..PINOUT_SCANNER_JTAG_PINS. Caller zeroes the
// iterator, then loops `while (pinout_perm_next(&it)) { ... }`. The
// k indices are read from `it.indices[0..k-1]`.
// -----------------------------------------------------------------------------

typedef struct {
    uint8_t indices[PINOUT_SCANNER_JTAG_PINS];
    uint8_t k;
    uint8_t n;
    bool    started;
} pinout_perm_iter_t;

void pinout_perm_init(pinout_perm_iter_t *it, uint8_t k, uint8_t n);
bool pinout_perm_next(pinout_perm_iter_t *it);

// Returns P(n, k) = n * (n-1) * ... * (n-k+1). Used by the shell's
// progress display and by tests. Returns 0 if k > n or k == 0.
uint32_t pinout_perm_total(uint8_t k, uint8_t n);

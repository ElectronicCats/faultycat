#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/gpio.h"

// services/jtag_core — JTAG bit-bang TAP controller (CPU-driven).
//
// Adapted from third_party/blueTag @ v2.1.2 (MIT, attribution at top
// of jtag_core.c). We port the function shapes — jtagConfig,
// restoreIdle, enterShiftDR, enterShiftIR, getDeviceIDs, detectDevices,
// bypassTest, calculateJtagPermutations, isValidDeviceID — and rewrite
// each one against hal/gpio so it lives inside the v3 layered model.
//
// FaultyCat-specific differences vs blueTag:
//   1. Pins (TDI/TDO/TMS/TCK/TRST) routed over the scanner header
//      (Conn_01x10, GP0..GP7) since v2.x has no dedicated JTAG header.
//      The operator picks four scanner channels at session start;
//      F8-2's pinout_scanner auto-discovers them.
//   2. CPU bit-bang via hal/gpio (no PIO). Push-pull drives are fine
//      through the TXS0108E level shifter for TCK/TMS/TDI (host-driven
//      unidirectional) and TDO (target-driven unidirectional). The
//      bidirectional-protocol bug that blocks SWD on this board does
//      NOT apply to JTAG — confirmed in HARDWARE_V2.md §2.
//   3. jtag_core and swd_phy share GP0..GP7. Only one may be inited
//      at a time. F8-1 enforces this with a shell-level check; F9
//      replaces with a pico-sdk mutex_t covering daplink_usb +
//      pinout_scanner too.
//
// Constants pulled from blueTag and IEEE 1149.1 Std:
//   - JTAG_MAX_DEVICES        : 32 devices in a single chain (caps
//                               the fall-through scan loops; real
//                               targets rarely exceed 4-8 devices).
//   - JTAG_MIN_IR_LEN         : 2 bits per IEEE 1149.1.
//   - JTAG_MAX_IR_LEN         : 32 bits per device.
//   - JTAG_MAX_DR_LEN         : 4096 bits — accepted for forward
//                               use; F8-1 doesn't shift DRs that
//                               wide yet.
//   - JTAG_PIN_TRST_NONE      : pass to jtag_pinout_t.trst when no
//                               TRST line is wired.

#define JTAG_MAX_DEVICES        32u
#define JTAG_MIN_IR_LEN         2u
#define JTAG_MAX_IR_LEN         32u
#define JTAG_MAX_DR_LEN         4096u
#define JTAG_PIN_TRST_NONE      ((int8_t)-1)

typedef struct {
    uint8_t tdi;
    uint8_t tdo;
    uint8_t tms;
    uint8_t tck;
    int8_t  trst;       // JTAG_PIN_TRST_NONE if no TRST wired
} jtag_pinout_t;

// Initialize the bit-bang phy on the given pinout.
// Configures TDI/TMS/TCK as outputs (TCK driven low at idle), TDO as
// input (with internal pull-up so an unconnected target floats high
// rather than producing random sample noise). Optional TRST is driven
// high (deasserted) on init.
//
// Returns false if `pins` is NULL, any pin is out of range, or any
// two of (tdi, tdo, tms, tck, trst) collide. Cross-service ownership
// (vs swd_phy) is checked at the SHELL level, NOT here — jtag_core
// will gladly init even if swd_phy already owns one of these pins.
bool jtag_init(const jtag_pinout_t *pins);

// Tear down: revert all four (or five) pins to plain GPIO inputs with
// pulls disabled, releasing them so scanner_io / swd_phy can re-claim.
// Safe to call repeatedly.
void jtag_deinit(void);

// True iff jtag_init succeeded since the last deinit.
bool jtag_is_inited(void);

// Drive the TAP to Test-Logic-Reset (5 TCK with TMS=1) and then to
// Run-Test/Idle (1 TCK with TMS=0). Always safe to call; resets the
// state machine regardless of where it currently is.
void jtag_reset_to_run_test_idle(void);

// Pulse TRST low for ~1 ms then release. No-op if jtag_init received
// JTAG_PIN_TRST_NONE.
void jtag_assert_trst(void);

// IDCODE chain readout. Detects how many devices are on the chain
// (via jtag_detect_chain_length internally), shifts 32 bits per
// device out of the IDCODE register (the post-reset default DR for
// IEEE 1149.1-compliant devices), bit-reverses each, and writes them
// in order into `out`. Returns the number of devices found, capped
// at min(detected, max_devices).
//
// out[i] is the canonical IDCODE: bit 0 always 1, bits[11:1] = mfg id,
// bits[27:12] = part #, bits[31:28] = version.
size_t jtag_read_idcodes(uint32_t *out, size_t max_devices);

// Validate an IDCODE per IEEE 1149.1: bit 0 must be 1, mfg id in
// [1..126], bank ≤ 8, value not 0 or 0xFFFFFFFF. Used by F8-2's
// scanner to filter false positives during the brute-force pinout
// search.
bool jtag_idcode_is_valid(uint32_t idc);

// blueTag-style devices-in-chain count. Algorithm: shift all-1s into
// every IR (selects BYPASS on every device), then shift a 0 through
// the chain of 1-bit BYPASS DRs and count clocks until it appears at
// TDO. Returns 0..JTAG_MAX_DEVICES; 0 means "no device detected".
size_t jtag_detect_chain_length(void);

// JTAGulator permutation count for an N-channel scanner header:
// P(N, 4) = N * (N-1) * (N-2) * (N-3). F8-2 uses this as the upper
// bound on the brute-force pinout search. Returns 0 if N < 4.
uint32_t jtag_permutations_count(uint32_t channels);

// 32-bit MSB↔LSB reverse — exposed for tests; jtag_read_idcodes uses
// it internally to flip on-the-wire LSB-first IDCODE bits into the
// canonical bit ordering.
uint32_t jtag_bit_reverse32(uint32_t v);

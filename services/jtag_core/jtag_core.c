/*
 * services/jtag_core/jtag_core.c — JTAG bit-bang TAP controller.
 *
 * Function shapes adapted from third_party/blueTag @ v2.1.2 under the
 * upstream MIT license. The original sources are vendored under
 * `third_party/blueTag/src/blueTag.c` (read-only reference); this file
 * is a fresh implementation that fits the FaultyCat v3 layered model
 * (hal/gpio + hal/time, no direct pico-sdk).
 *
 * Upstream functions this is structured after:
 *   blueTag.c::jtagConfig                → jtag_init
 *   blueTag.c::restoreIdle               → jtag_reset_to_run_test_idle
 *   blueTag.c::enterShiftDR / enterShiftIR → static enter_shift_*
 *   blueTag.c::tdoRead / tckPulse        → static tck_pulse_read_tdo
 *                                          / tck_pulse_no_read
 *   blueTag.c::getDeviceIDs              → jtag_read_idcodes
 *   blueTag.c::detectDevices             → jtag_detect_chain_length
 *   blueTag.c::isValidDeviceID           → jtag_idcode_is_valid
 *   blueTag.c::calculateJtagPermutations → jtag_permutations_count
 *   blueTag.c::bitReverse                → jtag_bit_reverse32
 *
 * Differences from upstream worth knowing:
 *   1. No pico-sdk gpio_* calls; everything routes through hal/gpio.
 *      Tests link against tests/hal_fake/gpio_fake.c with input
 *      scripts + edge sampler so TAP transitions are exercised
 *      without HW.
 *   2. Pin uniqueness is validated in jtag_init (blueTag relied on
 *      the operator) — duplicates would silently corrupt the wire.
 *   3. jtag_read_idcodes calls jtag_detect_chain_length first to get
 *      the device count, mirroring blueTag's two-step flow but
 *      packaged behind one entry point.
 *   4. TDO is configured input + internal pull-up. Without that, an
 *      unconnected target floats and produces random sample noise
 *      that breaks the IDCODE / bypass heuristics.
 *
 * --- MIT License (blueTag excerpt) -----------------------------------
 *   The MIT License (MIT)
 *   Copyright (c) 2024 Aodrulez
 *
 *   Permission is hereby granted, free of charge, to any person
 *   obtaining a copy of this software and associated documentation
 *   files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use,
 *   copy, modify, merge, publish, distribute, sublicense, and/or
 *   sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 * --------------------------------------------------------------------
 */

#include "jtag_core.h"

#include <stdbool.h>
#include <stdint.h>

#include "hal/gpio.h"
#include "hal/time.h"

// Module-static state. F8-1 keeps it singleton — only one JTAG engine
// can be active at a time on this board (8 scanner channels shared
// with swd_phy). F9 promotes ownership to a mutex_t.
static jtag_pinout_t s_pins;
static bool          s_inited = false;

// -----------------------------------------------------------------------------
// Internal bit-bang primitives
// -----------------------------------------------------------------------------

static inline void set_tms(bool v) { hal_gpio_put(s_pins.tms, v); }
static inline void set_tdi(bool v) { hal_gpio_put(s_pins.tdi, v); }

// Clock TCK low → high → low. TMS / TDI must be pre-set to the values
// the target should sample on the rising edge.
static inline void tck_pulse_no_read(void) {
    hal_gpio_put(s_pins.tck, true);
    hal_gpio_put(s_pins.tck, false);
}

// As tck_pulse_no_read but samples TDO during the high phase. IEEE
// 1149.1 specifies TDO updates on TCK falling edge and the host
// samples on rising edge — so we read between the two puts.
static inline bool tck_pulse_read_tdo(void) {
    hal_gpio_put(s_pins.tck, true);
    bool tdo = hal_gpio_get(s_pins.tdo);
    hal_gpio_put(s_pins.tck, false);
    return tdo;
}

// TAP navigation helpers. All assume the TAP is in Run-Test/Idle on
// entry and leave it in Shift-DR / Shift-IR on exit. Mirror blueTag's
// enterShiftDR / enterShiftIR exactly.

static void enter_shift_dr(void) {
    set_tms(true);  tck_pulse_no_read();   // Run-Test/Idle  → Select-DR-Scan
    set_tms(false); tck_pulse_no_read();   // Select-DR-Scan → Capture-DR
    set_tms(false); tck_pulse_no_read();   // Capture-DR     → Shift-DR
}

static void enter_shift_ir(void) {
    set_tms(true);  tck_pulse_no_read();   // Run-Test/Idle  → Select-DR-Scan
    set_tms(true);  tck_pulse_no_read();   // Select-DR-Scan → Select-IR-Scan
    set_tms(false); tck_pulse_no_read();   // Select-IR-Scan → Capture-IR
    set_tms(false); tck_pulse_no_read();   // Capture-IR     → Shift-IR
}

// Shift-{DR,IR} → Exit1-{DR,IR} → Update-{DR,IR} → Run-Test/Idle.
static void exit_shift_to_run_test_idle(void) {
    set_tms(true);  tck_pulse_no_read();   // Shift-*   → Exit1-*
    set_tms(true);  tck_pulse_no_read();   // Exit1-*   → Update-*
    set_tms(false); tck_pulse_no_read();   // Update-*  → Run-Test/Idle
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool jtag_init(const jtag_pinout_t *pins) {
    if (s_inited || pins == NULL) return false;

    // Bound check + uniqueness. RP2040 has 30 GPIOs but the v2.x
    // scanner header only exposes GP0..GP7; we accept any GP0..GP29
    // here so future board revs / loopback rigs aren't blocked.
    if (pins->tdi >= 30u || pins->tdo >= 30u
     || pins->tms >= 30u || pins->tck >= 30u) {
        return false;
    }
    if (pins->trst != JTAG_PIN_TRST_NONE
     && (pins->trst < 0 || pins->trst >= 30)) {
        return false;
    }
    if (pins->tdi == pins->tdo || pins->tdi == pins->tms
     || pins->tdi == pins->tck || pins->tdo == pins->tms
     || pins->tdo == pins->tck || pins->tms == pins->tck) {
        return false;
    }
    if (pins->trst != JTAG_PIN_TRST_NONE) {
        uint8_t t = (uint8_t)pins->trst;
        if (t == pins->tdi || t == pins->tdo
         || t == pins->tms || t == pins->tck) {
            return false;
        }
    }

    s_pins = *pins;

    hal_gpio_init(s_pins.tdi, HAL_GPIO_DIR_OUT);
    hal_gpio_init(s_pins.tms, HAL_GPIO_DIR_OUT);
    hal_gpio_init(s_pins.tck, HAL_GPIO_DIR_OUT);
    hal_gpio_init(s_pins.tdo, HAL_GPIO_DIR_IN);
    // Pull-up on TDO so an unconnected target floats high rather than
    // emitting random samples. The chain detector keys off all-1s as
    // its "no target" sentinel so a stable HIGH is exactly what we
    // want when the cable's not plugged in.
    hal_gpio_set_pulls(s_pins.tdo, true, false);

    hal_gpio_put(s_pins.tdi, false);
    hal_gpio_put(s_pins.tms, true);   // start with TMS high so the first
                                      // edge of jtag_reset_to_run_test_idle
                                      // already advances toward TLR.
    hal_gpio_put(s_pins.tck, false);

    if (s_pins.trst != JTAG_PIN_TRST_NONE) {
        uint8_t t = (uint8_t)s_pins.trst;
        hal_gpio_init(t, HAL_GPIO_DIR_OUT);
        hal_gpio_put(t, true);        // deasserted (TRST is active-low)
    }

    s_inited = true;
    return true;
}

void jtag_deinit(void) {
    if (!s_inited) return;
    hal_gpio_init(s_pins.tdi, HAL_GPIO_DIR_IN);
    hal_gpio_init(s_pins.tms, HAL_GPIO_DIR_IN);
    hal_gpio_init(s_pins.tck, HAL_GPIO_DIR_IN);
    hal_gpio_init(s_pins.tdo, HAL_GPIO_DIR_IN);
    hal_gpio_set_pulls(s_pins.tdi, false, false);
    hal_gpio_set_pulls(s_pins.tms, false, false);
    hal_gpio_set_pulls(s_pins.tck, false, false);
    hal_gpio_set_pulls(s_pins.tdo, false, false);
    if (s_pins.trst != JTAG_PIN_TRST_NONE) {
        uint8_t t = (uint8_t)s_pins.trst;
        hal_gpio_init(t, HAL_GPIO_DIR_IN);
        hal_gpio_set_pulls(t, false, false);
    }
    s_inited = false;
}

bool jtag_is_inited(void) {
    return s_inited;
}

void jtag_reset_to_run_test_idle(void) {
    if (!s_inited) return;
    set_tms(true);
    for (uint8_t i = 0; i < 5u; i++) tck_pulse_no_read();
    set_tms(false);
    tck_pulse_no_read();   // → Run-Test/Idle
}

void jtag_assert_trst(void) {
    if (!s_inited || s_pins.trst == JTAG_PIN_TRST_NONE) return;
    uint8_t t = (uint8_t)s_pins.trst;
    hal_gpio_put(t, false);
    hal_busy_wait_us(1000u);
    hal_gpio_put(t, true);
}

bool jtag_clock_bit(bool tms, bool tdi) {
    if (!s_inited) return false;
    set_tms(tms);
    set_tdi(tdi);
    return tck_pulse_read_tdo();
}

uint32_t jtag_bit_reverse32(uint32_t v) {
    uint32_t r = 0u;
    for (uint8_t i = 0; i < 32u; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

bool jtag_idcode_is_valid(uint32_t idc) {
    // IEEE 1149.1: IDCODE bit 0 must be 1; mfg id in [1..126]; bank
    // ≤ 8 (JEP106 has more banks but blueTag's vendor table only goes
    // that far and it's plenty for the scanner). Reject the floating-
    // bus sentinels.
    if (idc == 0u || idc == 0xFFFFFFFFu) return false;
    if ((idc & 1u) != 1u) return false;
    uint32_t bank = (idc >> 8) & 0xFu;
    uint32_t id   = (idc >> 1) & 0x7Fu;
    if (id < 1u || id > 126u) return false;
    if (bank > 8u) return false;
    return true;
}

uint32_t jtag_permutations_count(uint32_t channels) {
    if (channels < 4u) return 0u;
    return channels * (channels - 1u) * (channels - 2u) * (channels - 3u);
}

size_t jtag_detect_chain_length(void) {
    if (!s_inited) return 0u;

    jtag_reset_to_run_test_idle();
    enter_shift_ir();

    // Fill every device's IR with all 1s. By the IEEE 1149.1 mandatory
    // BYPASS instruction encoding (all-ones), this puts every device
    // into BYPASS, which has a 1-bit DR.
    set_tdi(true);
    for (uint16_t i = 0; i < (JTAG_MAX_DEVICES * JTAG_MAX_IR_LEN); i++) {
        tck_pulse_no_read();
    }

    // Manually walk Shift-IR → Exit1-IR → Update-IR → Select-DR-Scan
    // → Capture-DR → Shift-DR. (We don't reuse exit_shift_to_run_test_idle
    // because we want to land in Shift-DR, not Run-Test/Idle.)
    set_tms(true);  tck_pulse_no_read();   // Shift-IR        → Exit1-IR
    set_tms(true);  tck_pulse_no_read();   // Exit1-IR        → Update-IR (BYPASS in effect)
    set_tms(true);  tck_pulse_no_read();   // Update-IR       → Select-DR-Scan
    set_tms(false); tck_pulse_no_read();   // Select-DR-Scan  → Capture-DR
    set_tms(false); tck_pulse_no_read();   // Capture-DR      → Shift-DR

    // Pre-load every BYPASS DR with 1.
    set_tdi(true);
    for (uint16_t i = 0; i < JTAG_MAX_DEVICES; i++) tck_pulse_no_read();

    // Now shift a single 0 in and count clocks until it propagates
    // out at TDO. The clock count == chain length.
    set_tdi(false);
    size_t n = 0u;
    for (uint16_t i = 0; i < JTAG_MAX_DEVICES; i++) {
        if (tck_pulse_read_tdo() == false) {
            n = (size_t)i + 1u;
            break;
        }
    }

    exit_shift_to_run_test_idle();
    if (n > JTAG_MAX_DEVICES) n = 0u;
    return n;
}

size_t jtag_read_idcodes(uint32_t *out, size_t max_devices) {
    if (!s_inited || out == NULL || max_devices == 0u) return 0u;

    size_t chain = jtag_detect_chain_length();
    if (chain == 0u) return 0u;
    if (chain > max_devices) chain = max_devices;

    // After a TAP reset the default DR is IDCODE for IEEE 1149.1
    // compliant devices. Walk to Shift-DR and clock 32 bits per
    // device. TDI=1 keeps any non-compliant BYPASS slots benign.
    jtag_reset_to_run_test_idle();
    enter_shift_dr();

    set_tdi(true);
    set_tms(false);

    for (size_t d = 0; d < chain; d++) {
        uint32_t v = 0u;
        for (uint8_t b = 0; b < 32u; b++) {
            // Wire shifts LSB-first; we accumulate MSB-first so the
            // canonical bit order falls out after a single 32-bit
            // bit-reverse.
            v = (v << 1) | (tck_pulse_read_tdo() ? 1u : 0u);
        }
        out[d] = jtag_bit_reverse32(v);
    }

    exit_shift_to_run_test_idle();
    return chain;
}

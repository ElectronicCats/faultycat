/*
 * services/pinout_scanner/pinout_scanner.c — JTAGulator-style scan.
 *
 * Algorithm from third_party/blueTag @ v2.1.2 (MIT) — specifically
 * the brute-force loops in `jtagScan` and `swdScan`. Reimplemented
 * against services/jtag_core (F8-1) and services/swd_core (F6) so
 * we stay inside the v3 layered model.
 *
 * --- MIT License (blueTag excerpt) -----------------------------------
 *   The MIT License (MIT)
 *   Copyright (c) 2024 Aodrulez
 *
 *   Permission is hereby granted, free of charge, to any person
 *   obtaining a copy of this software and associated documentation
 *   files (the "Software"), to deal in the Software without
 *   restriction... THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
 *   WARRANTY OF ANY KIND ...
 *   (full license at third_party/blueTag/LICENSE +
 *    LICENSES/UPSTREAM-blueTag.txt)
 * --------------------------------------------------------------------
 */

#include "pinout_scanner.h"

#include <string.h>

#include "board_v2.h"
#include "jtag_core.h"
#include "swd_dp.h"
#include "swd_phy.h"

// Channel index → GPIO. On v2.x scanner header the mapping is
// identity for ch0..ch7 → GP0..GP7, but routing through this table
// keeps the algorithm portable across future board revs.
static const uint8_t s_ch_to_gpio[PINOUT_SCANNER_CHANNELS] = {
    BOARD_GP_SCANNER_CH0, BOARD_GP_SCANNER_CH1,
    BOARD_GP_SCANNER_CH2, BOARD_GP_SCANNER_CH3,
    BOARD_GP_SCANNER_CH4, BOARD_GP_SCANNER_CH5,
    BOARD_GP_SCANNER_CH6, BOARD_GP_SCANNER_CH7,
};

// -----------------------------------------------------------------------------
// Permutation iterator
// -----------------------------------------------------------------------------

void pinout_perm_init(pinout_perm_iter_t *it, uint8_t k, uint8_t n) {
    if (it == NULL) return;
    memset(it, 0, sizeof(*it));
    it->k = k;
    it->n = n;
}

uint32_t pinout_perm_total(uint8_t k, uint8_t n) {
    if (k == 0u || k > n) return 0u;
    uint32_t r = 1u;
    for (uint8_t i = 0; i < k; i++) {
        r *= (uint32_t)(n - i);
    }
    return r;
}

static bool perm_unique(const uint8_t *arr, uint8_t k) {
    // O(k^2) but k ≤ 4 — fine.
    for (uint8_t i = 0; i < k; i++) {
        for (uint8_t j = (uint8_t)(i + 1u); j < k; j++) {
            if (arr[i] == arr[j]) return false;
        }
    }
    return true;
}

bool pinout_perm_next(pinout_perm_iter_t *it) {
    if (it == NULL || it->k == 0u || it->k > it->n) return false;

    if (!it->started) {
        it->started = true;
        // Seed with [0, 1, 2, ..., k-1] — already unique and
        // lexicographically the smallest valid k-permutation.
        for (uint8_t i = 0; i < it->k; i++) it->indices[i] = i;
        return true;
    }

    // Walk a base-n counter over indices[0..k-1], discarding tuples
    // with duplicates. lexicographic order: increment the LAST slot
    // first (treating indices[0] as the most-significant digit).
    while (true) {
        int i = (int)it->k - 1;
        while (i >= 0) {
            it->indices[i] = (uint8_t)(it->indices[i] + 1u);
            if (it->indices[i] < it->n) break;
            it->indices[i] = 0u;
            i--;
        }
        if (i < 0) return false;   // counter rolled over → exhausted
        if (perm_unique(it->indices, it->k)) return true;
    }
}

// -----------------------------------------------------------------------------
// JTAG scan
// -----------------------------------------------------------------------------

bool pinout_scan_jtag(pinout_scan_jtag_result_t *out,
                      pinout_scanner_progress_cb cb) {
    if (out == NULL) return false;
    if (jtag_is_inited()) return false;   // shell soft-lock should have caught

    memset(out, 0, sizeof(*out));

    pinout_perm_iter_t it;
    pinout_perm_init(&it, PINOUT_SCANNER_JTAG_PINS, PINOUT_SCANNER_CHANNELS);

    uint32_t cur = 0u;
    while (pinout_perm_next(&it)) {
        if (cb != NULL) cb(cur, PINOUT_SCANNER_JTAG_TOTAL);
        cur++;

        jtag_pinout_t pins = {
            .tdi  = s_ch_to_gpio[it.indices[0]],
            .tdo  = s_ch_to_gpio[it.indices[1]],
            .tms  = s_ch_to_gpio[it.indices[2]],
            .tck  = s_ch_to_gpio[it.indices[3]],
            .trst = JTAG_PIN_TRST_NONE,
        };
        if (!jtag_init(&pins)) continue;

        uint32_t ids[JTAG_MAX_DEVICES];
        size_t   n = jtag_read_idcodes(ids, JTAG_MAX_DEVICES);
        if (n >= 1u && jtag_idcode_is_valid(ids[0])) {
            out->tdi          = pins.tdi;
            out->tdo          = pins.tdo;
            out->tms          = pins.tms;
            out->tck          = pins.tck;
            out->idcode       = ids[0];
            out->chain_length = n;
            jtag_deinit();
            return true;
        }
        jtag_deinit();
    }
    return false;
}

// -----------------------------------------------------------------------------
// SWD scan
// -----------------------------------------------------------------------------

bool pinout_scan_swd(uint32_t targetsel,
                     pinout_scan_swd_result_t *out,
                     pinout_scanner_progress_cb cb) {
    if (out == NULL) return false;

    memset(out, 0, sizeof(*out));

    pinout_perm_iter_t it;
    pinout_perm_init(&it, PINOUT_SCANNER_SWD_PINS, PINOUT_SCANNER_CHANNELS);

    uint32_t cur = 0u;
    while (pinout_perm_next(&it)) {
        if (cb != NULL) cb(cur, PINOUT_SCANNER_SWD_TOTAL);
        cur++;

        uint8_t swclk = s_ch_to_gpio[it.indices[0]];
        uint8_t swdio = s_ch_to_gpio[it.indices[1]];
        if (!swd_phy_init(swclk, swdio, SWD_PHY_NRST_NONE)) continue;

        uint32_t dpidr = 0u;
        swd_dp_ack_t ack = swd_dp_connect(targetsel, &dpidr);
        if (ack == SWD_ACK_OK && dpidr != 0u) {
            out->swclk     = swclk;
            out->swdio     = swdio;
            out->dpidr     = dpidr;
            out->targetsel = targetsel;
            swd_phy_deinit();
            return true;
        }
        swd_phy_deinit();
    }
    return false;
}

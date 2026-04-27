#include "swd_dp.h"

#include <stddef.h>

#include "swd_phy.h"

// SWD wire protocol on top of services/swd_phy. Reimplemented from
// scratch under BSD-3 against ARM IHI 0031 (ADIv5 specification);
// the free-dap (BSD-3) and CMSIS-DAP host-side parsers were used as
// architectural reference only.
//
// Request byte layout (8 bits, sent LSB-first on the wire):
//   bit 0 : START   (always 1)
//   bit 1 : APnDP   (0 = DP, 1 = AP)
//   bit 2 : RnW     (0 = write, 1 = read)
//   bit 3 : A2      (address bit 2)
//   bit 4 : A3      (address bit 3)
//   bit 5 : parity  (even parity over bits 1..4)
//   bit 6 : STOP    (always 0)
//   bit 7 : PARK    (always 1)
//
// Sequence per transfer (host = HOST, target = TGT):
//   HOST drives 8-bit request
//   1-cycle turnaround (HOST releases SWDIO)
//   TGT drives 3-bit ACK
//   on read,  TGT drives 32-bit data + 1-bit parity, then 1-cycle turnaround
//   on write, 1-cycle turnaround, then HOST drives 32-bit data + 1-bit parity

static uint8_t build_request(bool ap_n_dp, bool rnw, uint8_t addr) {
    // Only A[3:2] travel on the wire (A[1:0] are always 0 for word-
    // aligned 32-bit access).
    uint8_t a2 = (uint8_t)((addr >> 2) & 1u);
    uint8_t a3 = (uint8_t)((addr >> 3) & 1u);
    uint8_t fields = (uint8_t)((ap_n_dp ? 1u : 0u) << 1)
                   | (uint8_t)((rnw     ? 1u : 0u) << 2)
                   | (uint8_t)(a2 << 3)
                   | (uint8_t)(a3 << 4);
    uint8_t parity = (uint8_t)(((fields >> 1) ^ (fields >> 2)
                              ^ (fields >> 3) ^ (fields >> 4)) & 1u);
    return (uint8_t)(0x81u | fields | (uint8_t)(parity << 5));
    // 0x81 = bit 0 (START) + bit 7 (PARK).
}

uint8_t swd_dp_compute_parity(uint32_t v) {
    v ^= v >> 16;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (uint8_t)(v & 1u);
}

static swd_dp_ack_t do_transfer(bool ap_n_dp, bool rnw, uint8_t addr,
                                uint32_t *out, uint32_t in_val) {
    uint8_t req = build_request(ap_n_dp, rnw, addr);

    swd_phy_write_bits(8u, req);

    // Turnaround: SWCLK cycles with SWDIO in hi-z so the host can
    // release the line and the target can start driving the ACK.
    // hiz_clocks() actually generates the clock; the previous code
    // called read_mode() here, which only flipped pindirs without
    // toggling SWCLK — so the target's protocol state machine never
    // saw the turnaround edge and never drove ACK.
    // Cross-checked against free-dap dap.c::dap_swd_operation
    // (dap_swj_run(dap_swd_turnaround) between header and ACK).
    swd_phy_hiz_clocks(1u);

    uint32_t ack_bits = swd_phy_read_bits(3u);

    if (ack_bits == (uint32_t)SWD_ACK_OK) {
        if (rnw) {
            uint32_t data        = swd_phy_read_bits(32u);
            uint32_t parity_bit  = swd_phy_read_bits(1u);
            // Spec: 1-cycle turnaround back to host-driven SWDIO.
            swd_phy_hiz_clocks(1u);
            if ((swd_dp_compute_parity(data) & 1u) != (parity_bit & 1u)) {
                return SWD_ACK_PARITY_ERR;
            }
            if (out) *out = data;
            return SWD_ACK_OK;
        }
        // Write path: 1-cycle turnaround, then host drives data + parity.
        // (No write_mode() call — write_bits' dispatcher sets pindir=1
        // for us, and with the open-drain bitloop the per-bit pindir
        // matters more than the dispatcher's setting anyway.)
        swd_phy_hiz_clocks(1u);
        swd_phy_write_bits(32u, in_val);
        swd_phy_write_bits(1u,  swd_dp_compute_parity(in_val));
        return SWD_ACK_OK;
    }

    // ACK was WAIT, FAULT, or undefined. Per spec one turnaround
    // cycle is still owed before the next transfer to give the
    // target time to release SWDIO. Recovery (ABORT / re-send) is
    // the caller's responsibility.
    swd_phy_hiz_clocks(1u);

    if (ack_bits == (uint32_t)SWD_ACK_WAIT)  return SWD_ACK_WAIT;
    if (ack_bits == (uint32_t)SWD_ACK_FAULT) return SWD_ACK_FAULT;
    // Bus contention or no target — SWDIO stuck high reads as 0b111.
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_read(uint8_t addr, uint32_t *out) {
    return do_transfer(false, true, addr, out, 0u);
}

swd_dp_ack_t swd_dp_write(uint8_t addr, uint32_t val) {
    return do_transfer(false, false, addr, NULL, val);
}

swd_dp_ack_t swd_dp_ap_read(uint8_t bank_addr, uint32_t *out) {
    return do_transfer(true, true, bank_addr, out, 0u);
}

swd_dp_ack_t swd_dp_ap_write(uint8_t bank_addr, uint32_t val) {
    return do_transfer(true, false, bank_addr, NULL, val);
}

swd_dp_ack_t swd_dp_abort(uint32_t flags) {
    return swd_dp_write(SWD_DP_ADDR_ABORT, flags);
}

swd_dp_ack_t swd_dp_read_dpidr(uint32_t *out) {
    return swd_dp_read(SWD_DP_ADDR_DPIDR, out);
}

// Modern SWD wakeup byte stream (ADIv5.2 §B5.4 dormant-to-SWD).
// Byte-for-byte aligned with OpenOCD `swd_seq_dormant_to_swd[]` in
// jtag/swd.h (224-bit total) and pyOCD `swj.py`. Backward-compatible
// with non-dormant targets — the alert+activation appear as extra
// line-reset cycles to a target already in SWD state.
//
// Wire content (LSB-first within each byte, byte 0 first):
//   0xff                          : 8 high (abort prior alert)
//   0x92..0x19  (16 bytes)        : 128-bit selection alert
//   0xa0                          : 4 low + activation low nibble
//   0xf1                          : activation high nibble + 4 high
//   0xff                          : 8 more high (continues line reset)
//   0xff×7                        : 56 more high (≥50 line reset total)
//   0x00                          : 8 low (≥2 idle)
static const uint8_t s_dormant_to_swd[] = {
    0xffu,
    0x92u, 0xf3u, 0x09u, 0x62u, 0x95u, 0x2du, 0x85u, 0x86u,
    0xe9u, 0xafu, 0xddu, 0xe3u, 0xa2u, 0x0eu, 0xbcu, 0x19u,
    0xa0u, 0xf1u, 0xffu,
    0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu,
    0x00u,
};

// Send a byte stream LSB-first on the wire, packing 4 bytes per
// 32-bit PIO call to minimize dispatcher overhead. The PIO uses
// out_shift_right=true, so packing little-endian (byte[i] in the
// low byte) sends bytes in array order, each byte LSB-first.
static void send_byte_stream(const uint8_t *bytes, uint32_t len) {
    uint32_t i = 0u;
    while (i + 4u <= len) {
        uint32_t v = (uint32_t)bytes[i]
                   | ((uint32_t)bytes[i + 1u] << 8)
                   | ((uint32_t)bytes[i + 2u] << 16)
                   | ((uint32_t)bytes[i + 3u] << 24);
        swd_phy_write_bits(32u, v);
        i += 4u;
    }
    while (i < len) {
        swd_phy_write_bits(8u, bytes[i]);
        i++;
    }
}

// SWDv2 TARGETSEL write. Multi-drop convention: ALL DPs see the
// request and ACK cycles, then only the DP whose TARGETID matches
// the data accepts subsequent transactions. The target does NOT
// drive ACK during TARGETSEL; the host clocks 3 ACK cycles for
// protocol alignment and discards whatever it reads.
//
// Request byte 0x99 = TARGETSEL (DP write, A[3:2]=11, parity=1).
static void targetsel_write(uint32_t targetsel) {
    swd_phy_write_bits(8u, 0x99u);
    swd_phy_hiz_clocks(1u);            // turnaround → host hi-z
    (void)swd_phy_read_bits(3u);       // ACK clocks; result discarded
    swd_phy_hiz_clocks(1u);            // turnaround → host driving
    swd_phy_write_bits(32u, targetsel);
    swd_phy_write_bits(1u,  swd_dp_compute_parity(targetsel));
    // 8 idle bits LOW so the bus settles before the next request.
    swd_phy_write_bits(8u, 0u);
}

swd_dp_ack_t swd_dp_connect(uint32_t targetsel, uint32_t *out_dpidr) {
    // Canonical "force into SWD" sequence per pyOCD
    // SWJSequenceSender.switch_to_swd() with use_dormant=True. We
    // don't know the initial state of the target (could be SWD,
    // JTAG, or dormant) so we walk through every transition:
    //
    //   1. Line reset — puts SWD in known state OR enters JTAG TLR.
    //   2. JTAG-to-dormant select (39 bits 0x33bbbbba LSB-first) —
    //      takes a JTAG TAP from TLR to dormant. No-op if already
    //      dormant. Per ADIv6 §B5.3.2.
    //   3. selection_alert + activation — wake from dormant into
    //      SWD. ADIv5.2 §B5.4 / ADIv6 §B5.3.3.
    //   4. Final line reset + 2 idle cycles — SWD reset of the now-
    //      awake DP.
    //   5. Pre-TARGETSEL line reset — OpenOCD prefixes TARGETSEL.
    //   6. TARGETSEL multi-drop write.
    //   7. DPIDR read.

    // Step 1: 51+ high (line reset) — we send 56 = 7 bytes 0xff.
    for (int i = 0; i < 7; i++) {
        swd_phy_write_bits(8u, 0xFFu);
    }

    // Step 2: JTAG-to-dormant select. 39 bits, LSB-first 0x33bbbbba.
    // Split into one 32-bit + one 7-bit write because write_bits
    // caps at 32. Sequence value is 31 significant bits + zero
    // padding to 32; the trailing 7 zeros act as idle low cycles.
    swd_phy_write_bits(32u, 0x33bbbbbau);
    swd_phy_write_bits(7u,  0x00u);

    // Steps 3 + 4: dormant-to-SWD byte stream (selection alert +
    // activation + line reset + idle).
    send_byte_stream(s_dormant_to_swd, (uint32_t)sizeof(s_dormant_to_swd));

    // Step 5: pre-TARGETSEL line reset.
    for (int i = 0; i < 7; i++) {
        swd_phy_write_bits(8u, 0xFFu);
    }

    // Step 6: TARGETSEL — selects which RP2040 DP responds to
    // subsequent DPIDR. Without this, RP2040's DPs stay silent.
    targetsel_write(targetsel);

    // Step 7: DPIDR read — should return 0x0BC12477 for RP2040.
    return swd_dp_read_dpidr(out_dpidr);
}

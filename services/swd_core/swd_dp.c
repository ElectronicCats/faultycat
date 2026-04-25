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

    swd_phy_write_mode();
    swd_phy_write_bits(8u, req);

    // Turnaround: 1 cycle hi-z so the target can start driving SWDIO
    // before the ACK. swd_phy_read_mode flips the pin direction on
    // the next dispatch; the read_bits below begins from the target's
    // first ACK bit.
    swd_phy_read_mode();

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
        swd_phy_hiz_clocks(1u);
        swd_phy_write_mode();
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

swd_dp_ack_t swd_dp_connect(uint32_t *out_dpidr) {
    // Line reset: ≥50 SWCLKs with SWDIO HIGH. Send 56 (7 bytes).
    swd_phy_write_mode();
    for (int i = 0; i < 7; i++) {
        swd_phy_write_bits(8u, 0xFFu);
    }
    // JTAG-to-SWD select: 0xE79E sent LSB-first → wire pattern
    // 0111 1001 1110 0111 per ARM IHI 0031 §B5.2.2.
    swd_phy_write_bits(16u, 0xE79Eu);
    // Second line reset, ≥50 ones.
    for (int i = 0; i < 7; i++) {
        swd_phy_write_bits(8u, 0xFFu);
    }
    // 8 idle bits (SWDIO low) before the first transaction.
    swd_phy_write_bits(8u, 0u);
    // First read after reset MUST be DPIDR per the spec.
    return swd_dp_read_dpidr(out_dpidr);
}

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
    uint8_t a2     = (uint8_t)((addr >> 2) & 1u);
    uint8_t a3     = (uint8_t)((addr >> 3) & 1u);
    uint8_t fields = (uint8_t)((ap_n_dp ? 1u : 0u) << 1) | (uint8_t)((rnw ? 1u : 0u) << 2) |
                     (uint8_t)(a2 << 3) | (uint8_t)(a3 << 4);
    uint8_t parity =
        (uint8_t)(((fields >> 1) ^ (fields >> 2) ^ (fields >> 3) ^ (fields >> 4)) & 1u);
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

bool swd_dp_dpidr_is_valid(uint32_t dpidr) {
    // ADIv5 DPIDR / IDCODE-like values vary by silicon, so this is
    // intentionally a coherence check rather than a target allowlist.
    // It rejects common floating-bus/no-target sentinels and requires
    // the architected ID bit plus non-empty designer/version fields.
    if (dpidr == 0u || dpidr == 0xFFFFFFFFu)
        return false;
    if ((dpidr & 1u) != 1u)
        return false;

    uint32_t designer = (dpidr >> 1) & 0x7FFu;
    uint32_t version  = (dpidr >> 12) & 0xFu;
    uint32_t partno   = (dpidr >> 20) & 0xFFu;
    if (designer == 0u || designer == 0x7FFu)
        return false;
    if (version == 0u)
        return false;
    if (partno == 0u)
        return false;
    return true;
}

static swd_dp_ack_t do_transfer(bool ap_n_dp, bool rnw, uint8_t addr, uint32_t* out,
                                uint32_t in_val) {
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
            uint32_t data       = swd_phy_read_bits(32u);
            uint32_t parity_bit = swd_phy_read_bits(1u);
            // Spec: 1-cycle turnaround back to host-driven SWDIO.
            swd_phy_hiz_clocks(1u);
            if ((swd_dp_compute_parity(data) & 1u) != (parity_bit & 1u)) {
                return SWD_ACK_PARITY_ERR;
            }
            if (out)
                *out = data;
            return SWD_ACK_OK;
        }
        // Write path: 1-cycle turnaround, then host drives data + parity.
        // (No write_mode() call — write_bits' dispatcher sets pindir=1
        // for us, and with the open-drain bitloop the per-bit pindir
        // matters more than the dispatcher's setting anyway.)
        swd_phy_hiz_clocks(1u);
        swd_phy_write_bits(32u, in_val);
        swd_phy_write_bits(1u, swd_dp_compute_parity(in_val));
        return SWD_ACK_OK;
    }

    // ACK was WAIT, FAULT, or undefined. Per spec one turnaround
    // cycle is still owed before the next transfer to give the
    // target time to release SWDIO. Recovery (ABORT / re-send) is
    // the caller's responsibility.
    swd_phy_hiz_clocks(1u);

    if (ack_bits == (uint32_t)SWD_ACK_WAIT)
        return SWD_ACK_WAIT;
    if (ack_bits == (uint32_t)SWD_ACK_FAULT)
        return SWD_ACK_FAULT;
    // Bus contention or no target — SWDIO stuck high reads as 0b111.
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_read(uint8_t addr, uint32_t* out) {
    return do_transfer(false, true, addr, out, 0u);
}

swd_dp_ack_t swd_dp_write(uint8_t addr, uint32_t val) {
    return do_transfer(false, false, addr, NULL, val);
}

swd_dp_ack_t swd_dp_ap_read(uint8_t bank_addr, uint32_t* out) {
    return do_transfer(true, true, bank_addr, out, 0u);
}

swd_dp_ack_t swd_dp_ap_write(uint8_t bank_addr, uint32_t val) {
    return do_transfer(true, false, bank_addr, NULL, val);
}

swd_dp_ack_t swd_dp_abort(uint32_t flags) {
    return swd_dp_write(SWD_DP_ADDR_ABORT, flags);
}

swd_dp_ack_t swd_dp_read_dpidr(uint32_t* out) {
    return swd_dp_read(SWD_DP_ADDR_DPIDR, out);
}

swd_dp_ack_t swd_dp_power_up(uint32_t max_retries) {
    // Default poll budget. At 1 MHz SWCLK each round-trip is roughly
    // a few microseconds, so 1000 retries ≈ a handful of ms — long
    // enough for any well-behaved DP to ack, short enough to fail
    // fast on a gated power domain or a dead bus.
    if (max_retries == 0u)
        max_retries = 1000u;

    // 1. Clear sticky errors so a stale STKERR / WDERR from a prior
    //    aborted transfer doesn't block the upcoming CTRL/STAT write.
    swd_dp_ack_t ack = swd_dp_abort(SWD_ABORT_ALL_STKY_CLR);
    if (ack != SWD_ACK_OK)
        return ack;

    // 2. Request system + debug power-up.
    ack = swd_dp_write(SWD_DP_ADDR_CTRLSTAT, SWD_CTRLSTAT_PWRUP_REQ);
    if (ack != SWD_ACK_OK)
        return ack;

    // 3. Poll CTRL/STAT until both ACKs come up.
    uint32_t value = 0u;
    for (uint32_t i = 0u; i < max_retries; ++i) {
        ack = swd_dp_read(SWD_DP_ADDR_CTRLSTAT, &value);
        if (ack != SWD_ACK_OK)
            return ack;
        if ((value & SWD_CTRLSTAT_PWRUP_ACK) == SWD_CTRLSTAT_PWRUP_ACK) {
            return SWD_ACK_OK;
        }
    }
    return SWD_ACK_WAIT;
}

void swd_dp_wakeup(void) {
    // ADIv5.2 dormant-to-SWD wake-up sequence. swd_phy_write_bits()
    // shifts LSB-first on the wire, so these 32-bit words are the
    // packed form of the bit strings in the spec/operator notes:
    //   8 HIGH alert-reset bits
    //   128-bit selection alert
    //   4 LOW idle bits
    //   8-bit SWD activation code 0x1A
    swd_phy_write_bits(8u, 0xFFu);

    swd_phy_write_bits(32u, 0x6209F392u);
    swd_phy_write_bits(32u, 0x86852D95u);
    swd_phy_write_bits(32u, 0xE3DDAFE9u);
    swd_phy_write_bits(32u, 0x19BC0EA2u);

    swd_phy_write_bits(4u, 0x0u);
    swd_phy_write_bits(8u, 0x1Au);
}

void swd_dp_switch_jtag_to_swd(void) {
    // ARM's SWJ JTAG-to-SWD select sequence:
    //   line reset (at least 50 HIGH cycles; use 56)
    //   16-bit JTAG_TO_SWD command 0xE79E
    //   line reset (at least 50 HIGH cycles; use 56)
    for (int i = 0; i < 10; i++) {
        swd_phy_write_bits(8u, 0xFFu);
    }

    swd_phy_write_bits(16u, 0xE79Eu);

    for (int i = 0; i < 10; i++) {
        swd_phy_write_bits(8u, 0xFFu);
    }

    swd_phy_write_bits(4u, 0x0u);
}

swd_dp_ack_t swd_dp_request_idcode(uint32_t* out_idcode) {
    // Reuse the normal DP-read transfer helper. For DP address 0
    // with RnW=1 it builds request byte 0xA5, then handles the
    // turnaround, ACK, 32 data bits, and parity check.
    swd_dp_ack_t ack = swd_dp_read(SWD_DP_ADDR_DPIDR, out_idcode);
    // Leave the line in idle-low before any follow-up transaction.
    swd_phy_write_bits(8u, 0x00u);
    return ack;
}

swd_dp_ack_t swd_dp_bus_detect(uint32_t* out_dpidr) {
    swd_dp_wakeup();
    swd_dp_switch_jtag_to_swd();
    return swd_dp_request_idcode(out_dpidr);
}

// Modern SWD wakeup byte stream (ADIv5.2 dormant-to-SWD), kept for
// the targeted TARGETSEL connect path. It includes the selection
// alert, activation, final line reset, and idle cycles in the
// byte-packed form used by OpenOCD/pyOCD.
static const uint8_t s_dormant_to_swd[] = {
    0xffu, 0x92u, 0xf3u, 0x09u, 0x62u, 0x95u, 0x2du, 0x85u, 0x86u, 0xe9u,
    0xafu, 0xddu, 0xe3u, 0xa2u, 0x0eu, 0xbcu, 0x19u, 0xa0u, 0xf1u, 0xffu,
    0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0xffu, 0x00u,
};

static void send_byte_stream(const uint8_t* bytes, uint32_t len) {
    uint32_t i = 0u;
    while (i + 4u <= len) {
        uint32_t v = (uint32_t)bytes[i] | ((uint32_t)bytes[i + 1u] << 8) |
                     ((uint32_t)bytes[i + 2u] << 16) | ((uint32_t)bytes[i + 3u] << 24);
        swd_phy_write_bits(32u, v);
        i += 4u;
    }
    while (i < len) {
        swd_phy_write_bits(8u, bytes[i]);
        i++;
    }
}

static void targetsel_write(uint32_t targetsel) {
    // SWDv2 TARGETSEL write. The multi-drop convention has all DPs
    // observe the request/data; the ACK clocks are still emitted for
    // wire alignment, but the value read during those cycles is not
    // meaningful.
    swd_phy_write_bits(8u, 0x99u);
    swd_phy_hiz_clocks(1u);
    (void)swd_phy_read_bits(3u);
    swd_phy_hiz_clocks(1u);
    swd_phy_write_bits(32u, targetsel);
    swd_phy_write_bits(1u, swd_dp_compute_parity(targetsel));
    swd_phy_write_bits(8u, 0u);
}

swd_dp_ack_t swd_dp_connect(uint32_t targetsel, uint32_t* out_dpidr) {
    swd_dp_abort(SWD_ABORT_DAPABORT);

    // Targeted SWDv2 connect for multi-drop-capable DPs (RP2040 is
    // the board-local reason this exists). This restores the original
    // flow: force a known state, wake dormant SWD, issue TARGETSEL,
    // then read DPIDR from the selected DP.
    // for (int i = 0; i < 7; i++) {
    //    swd_phy_write_bits(8u, 0xffu);
    // }

    // JTAG-to-dormant select, 39 bits, LSB-first 0x33bbbbba.
    // swd_phy_write_bits(32u, 0x33bbbbbau);
    // swd_phy_write_bits(7u,  0x00u);

    // send_byte_stream(s_dormant_to_swd, (uint32_t)sizeof(s_dormant_to_swd));

    // for (int i = 0; i < 7; i++) {
    //    swd_phy_write_bits(8u, 0xffu);
    // }

    // targetsel_write(targetsel);
    return swd_dp_read_dpidr(out_dpidr);
}

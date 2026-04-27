#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/swd_core/swd_dp — SWD wire protocol (DP: Debug Port).
//
// Reimplemented from scratch under BSD-3 on top of services/swd_phy.
// References (architectural only, no code copied): ARM
// IHI 0031 (ADIv5 specification), free-dap (BSD-3) wire layer,
// debugprobe/CMSIS-DAP host-side parser. Sits between swd_phy
// (raw bit shifting) and swd_mem / CMSIS-DAP (memory access).
//
// Frame layout (per transfer):
//   8-bit request    : start | APnDP | RnW | A2 | A3 | parity | stop | park
//   1 trn cycle (host hi-z so target can drive ACK)
//   3-bit ACK        : OK=001 | WAIT=010 | FAULT=100
//   on read  : 32-bit data + parity
//   on write : 1 trn cycle, then 32-bit data + parity
//   8 idle bits (per CMSIS-DAP convention) end the transfer.

typedef enum {
    SWD_ACK_OK    = 0x1,
    SWD_ACK_WAIT  = 0x2,
    SWD_ACK_FAULT = 0x4,
    SWD_ACK_PARITY_ERR = 0x8,   // local: data parity check failed
    SWD_ACK_NO_TARGET  = 0x7,   // local: SWDIO stuck (line reset failed)
} swd_dp_ack_t;

// DP register addresses (A[3:2] in the request).
#define SWD_DP_ADDR_DPIDR     0x00u   // RO — DP IDCODE / DPIDR
#define SWD_DP_ADDR_ABORT     0x00u   // WO
#define SWD_DP_ADDR_CTRLSTAT  0x04u   // RW
#define SWD_DP_ADDR_SELECT    0x08u   // WO — APSEL + APBANKSEL
#define SWD_DP_ADDR_RDBUFF    0x0Cu   // RO — buffered AP read

// ABORT flags
#define SWD_ABORT_DAPABORT    (1u << 0)
#define SWD_ABORT_STKCMPCLR   (1u << 1)
#define SWD_ABORT_STKERRCLR   (1u << 2)
#define SWD_ABORT_WDERRCLR    (1u << 3)
#define SWD_ABORT_ORUNERRCLR  (1u << 4)

// SWDv2 multi-drop TARGETID values. RP2040 has two M0+ cores
// addressable as separate DPs sharing one SWD bus. ADIv5.2 §B5.4
// makes TARGETSEL mandatory before DPIDR even when only one DP is
// physically present — RP2040's DPs ignore DPIDR until they see
// their own TARGETID land in the multi-drop write.
#define SWD_DP_TARGETSEL_RP2040_CORE0  0x01002927u
#define SWD_DP_TARGETSEL_RP2040_CORE1  0x11002927u
#define SWD_DP_TARGETSEL_RP2040_RESCUE 0xF1002927u

// Initialize DP-layer state. Must be called after swd_phy_init.
// Performs:
//   1. dormant-to-SWD wakeup (ADIv5.2 §B5.4 selection alert +
//      activation code 0x1A) — required because RP2040 boots in
//      dormant state. Backward-compatible with non-dormant targets.
//   2. line reset (≥50 SWCLKs SWDIO HIGH).
//   3. TARGETSEL write of `targetsel` (one of SWD_DP_TARGETSEL_*).
//      ACK is unacked per multi-drop convention; we clock 3 cycles
//      and discard the result.
//   4. DPIDR read.
// Returns ACK from the DPIDR read. On ACK_OK the DPIDR is written
// to *out_dpidr and the link is considered established.
swd_dp_ack_t swd_dp_connect(uint32_t targetsel, uint32_t *out_dpidr);

// Read DPIDR explicitly (also updates DP_SELECT to bank 0).
swd_dp_ack_t swd_dp_read_dpidr(uint32_t *out);

// Generic DP read/write. `addr` is one of SWD_DP_ADDR_*.
swd_dp_ack_t swd_dp_read(uint8_t addr, uint32_t *out);
swd_dp_ack_t swd_dp_write(uint8_t addr, uint32_t val);

// Generic AP read/write. The caller is responsible for setting
// DP_SELECT.APSEL/APBANKSEL via swd_dp_write(SELECT, ...) beforehand.
// AP reads are pipelined: the value returned is the result of the
// PREVIOUS AP read, per ADIv5. swd_mem handles the discard.
swd_dp_ack_t swd_dp_ap_read(uint8_t bank_addr, uint32_t *out);
swd_dp_ack_t swd_dp_ap_write(uint8_t bank_addr, uint32_t val);

// ABORT register write — clears sticky errors. Always succeeds at
// the wire level (CMSIS-DAP treats ABORT specially).
swd_dp_ack_t swd_dp_abort(uint32_t flags);

// Even-parity helper. Returns 0 if the count of 1 bits in `v` is
// even, 1 if odd. Exposed for tests.
uint8_t swd_dp_compute_parity(uint32_t v);

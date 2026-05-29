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
    SWD_ACK_OK         = 0x1,
    SWD_ACK_WAIT       = 0x2,
    SWD_ACK_FAULT      = 0x4,
    SWD_ACK_PARITY_ERR = 0x8, // local: data parity check failed
    SWD_ACK_NO_TARGET  = 0x7, // local: SWDIO stuck (line reset failed)
} swd_dp_ack_t;

// DP register addresses (A[3:2] in the request).
#define SWD_DP_ADDR_DPIDR    0x00u // RO — DP IDCODE / DPIDR
#define SWD_DP_ADDR_ABORT    0x00u // WO
#define SWD_DP_ADDR_CTRLSTAT 0x04u // RW
#define SWD_DP_ADDR_SELECT   0x08u // WO — APSEL + APBANKSEL
#define SWD_DP_ADDR_RDBUFF   0x0Cu // RO — buffered AP read

// ABORT flags
#define SWD_ABORT_DAPABORT   (1u << 0)
#define SWD_ABORT_STKCMPCLR  (1u << 1)
#define SWD_ABORT_STKERRCLR  (1u << 2)
#define SWD_ABORT_WDERRCLR   (1u << 3)
#define SWD_ABORT_ORUNERRCLR (1u << 4)

// All sticky-error CLR bits ORed together (everything in ABORT except
// DAPABORT, which would cancel any AP transaction in flight). Used by
// swd_dp_power_up() to wipe the slate before requesting power-up.
#define SWD_ABORT_ALL_STKY_CLR                                                                     \
    (SWD_ABORT_STKCMPCLR | SWD_ABORT_STKERRCLR | SWD_ABORT_WDERRCLR | SWD_ABORT_ORUNERRCLR)

// DP CTRL/STAT (ADIv5 §B2.2.2). Only the power-up request/ack bits we
// actually use are defined here; the rest of the register lives in the
// spec.
#define SWD_CTRLSTAT_CDBGPWRUPREQ (1u << 28)
#define SWD_CTRLSTAT_CDBGPWRUPACK (1u << 29)
#define SWD_CTRLSTAT_CSYSPWRUPREQ (1u << 30)
#define SWD_CTRLSTAT_CSYSPWRUPACK (1u << 31)
#define SWD_CTRLSTAT_PWRUP_REQ    (SWD_CTRLSTAT_CDBGPWRUPREQ | SWD_CTRLSTAT_CSYSPWRUPREQ)
#define SWD_CTRLSTAT_PWRUP_ACK    (SWD_CTRLSTAT_CDBGPWRUPACK | SWD_CTRLSTAT_CSYSPWRUPACK)

// SWDv2 multi-drop TARGETID values. RP2040 has two M0+ cores
// addressable as separate DPs sharing one SWD bus; TARGETSEL chooses
// which DP responds after the dormant-to-SWD wake-up sequence.
#define SWD_DP_TARGETSEL_RP2040_CORE0  0x01002927u
#define SWD_DP_TARGETSEL_RP2040_CORE1  0x11002927u
#define SWD_DP_TARGETSEL_RP2040_RESCUE 0xF1002927u

// SWD wake-up sequence (ADIv5.2 dormant-to-SWD): alert reset,
// 128-bit selection alert, 4 idle LOW bits, activation code 0x1A.
void swd_dp_wakeup(void);

// SWJ switch sequence: line reset, JTAG_TO_SWD command 0xE79E,
// line reset.
void swd_dp_switch_jtag_to_swd(void);

// Request the SWD IDCODE/DPIDR with request byte 0xA5, then emit
// 8 idle LOW bits. Returns the ACK from the IDCODE read.
swd_dp_ack_t swd_dp_request_idcode(uint32_t* out_idcode);

// Generic SWD bus detection. Performs wake-up -> JTAG-to-SWD ->
// IDCODE request without TARGETSEL, so it can identify any coherent
// SWD DP on the bus. Used by the brute-force SWD pinout scanner.
swd_dp_ack_t swd_dp_bus_detect(uint32_t* out_dpidr);

// Initialize DP-layer state. Must be called after swd_phy_init.
// Performs the targeted dormant-to-SWD + TARGETSEL sequence and then
// reads DPIDR from the selected DP. On ACK_OK the DPIDR is written
// to *out_dpidr.
swd_dp_ack_t swd_dp_connect(uint32_t targetsel, uint32_t* out_dpidr);

// Read DPIDR explicitly (also updates DP_SELECT to bank 0).
swd_dp_ack_t swd_dp_read_dpidr(uint32_t* out);

// Generic DP read/write. `addr` is one of SWD_DP_ADDR_*.
swd_dp_ack_t swd_dp_read(uint8_t addr, uint32_t* out);
swd_dp_ack_t swd_dp_write(uint8_t addr, uint32_t val);

// Generic AP read/write. The caller is responsible for setting
// DP_SELECT.APSEL/APBANKSEL via swd_dp_write(SELECT, ...) beforehand.
// AP reads are pipelined: the value returned is the result of the
// PREVIOUS AP read, per ADIv5. swd_mem handles the discard.
swd_dp_ack_t swd_dp_ap_read(uint8_t bank_addr, uint32_t* out);
swd_dp_ack_t swd_dp_ap_write(uint8_t bank_addr, uint32_t val);

// ABORT register write — clears sticky errors. Always succeeds at
// the wire level (CMSIS-DAP treats ABORT specially).
swd_dp_ack_t swd_dp_abort(uint32_t flags);

// Clear sticky errors + request system & debug power-up, then poll
// CTRL/STAT until both CSYSPWRUPACK and CDBGPWRUPACK are set.
//
// Standard ADIv5 first-contact sequence after `swd_dp_connect()` /
// `swd_dp_bus_detect()` reports OK. Must be called before any AP
// transaction (MEM-AP read32 / write32, scanning, etc.); without
// power-up the AP returns FAULT / WAIT or pure zeroes.
//
// Returns:
//   SWD_ACK_OK              — both power-up ACKs observed within the
//                             retry budget.
//   SWD_ACK_WAIT            — target never set the ACK bits before
//                             `max_retries` exhausted (treat as a
//                             timeout — power domain may be gated
//                             off / target unresponsive).
//   any other ack_t value   — propagated verbatim from the failing
//                             swd_dp_write / swd_dp_read; sticky
//                             state was not reached.
//
// `max_retries` of 0 falls back to a sensible default (~1000 polls,
// ~ a few ms at 1 MHz SWCLK).
swd_dp_ack_t swd_dp_power_up(uint32_t max_retries);

// Even-parity helper. Returns 0 if the count of 1 bits in `v` is
// even, 1 if odd. Exposed for tests.
uint8_t swd_dp_compute_parity(uint32_t v);

// Coherence check for SWD DP IDCODE/DPIDR values. This is not an
// RP2040 allowlist: it accepts any non-sentinel value with the
// architected ID bit set and non-empty designer/version/part fields.
// Used by the pinout scanner to reject obvious floating-bus noise
// while still allowing targets other than RP2040.
bool swd_dp_dpidr_is_valid(uint32_t dpidr);

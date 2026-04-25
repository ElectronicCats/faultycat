#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "swd_dp.h"   // for swd_dp_ack_t

// services/swd_core/swd_mem — MEM-AP layer (ADIv5).
//
// Sits on top of swd_dp. Configures the MEM-AP CSW for 32-bit
// access and exposes single-word read/write. Auto-increment on TAR
// is enabled so back-to-back reads/writes inside a 1 KB region
// don't need a TAR rewrite per access — that optimization matters
// for F9 campaign manager bulk verification reads.
//
// Multi-AP not supported in F6 — the MEM-AP is assumed to be AP 0
// (the case for every Cortex-M target we care about). F7 (full
// CMSIS-DAP) and F9 (campaign verification) extend if needed.

// Initialize the MEM-AP: select AP 0, write CSW for 32-bit auto-inc.
// Must be called after swd_dp_connect returned OK. Returns the ACK
// from the underlying DP transactions.
swd_dp_ack_t swd_mem_init(void);

// Single 32-bit memory read. addr must be 4-byte aligned. Returns
// OK on success and writes the read word to *out; on WAIT/FAULT/
// parity error returns the corresponding ACK and *out is unchanged.
swd_dp_ack_t swd_mem_read32(uint32_t addr, uint32_t *out);

// Single 32-bit memory write. addr must be 4-byte aligned.
swd_dp_ack_t swd_mem_write32(uint32_t addr, uint32_t val);

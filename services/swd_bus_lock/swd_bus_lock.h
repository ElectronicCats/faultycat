#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/swd_bus_lock — F9-1 mutual-exclusion wrapper over the
// scanner-header SWD/JTAG bus shared by F6 swd_core, F8-1 jtag_core,
// F8-2 pinout_scanner, F8-4 buspirate_compat, F9 campaign_manager,
// and the future F7 daplink_usb. Sits at the *service* layer — F8-1's
// shell-level soft-lock between SWD and JTAG (in apps/faultycat_fw/
// main.c) stays in place; this is orthogonal and covers the
// service-to-service contention case.
//
// Single-core cooperative model: no IRQ-side acquires (the F4/F5
// PIO IRQs never touch SWD), so a plain volatile flag with explicit
// owner tag is enough. We don't link pico-sdk's mutex_t directly —
// keeps host tests trivial.
//
// Static priority is documented in the plan and HARDWARE_V2.md but
// not enforced by this lock — F9-1 is exclusion only. The priority
// rule lives at the call sites (campaign / scanner / daplink decide
// who can wait for whom). The lock itself is FIFO-fair: first
// successful acquire wins until released.
//
// DAPLINK contract (plan §4 mutex SWD #3): when the F7 CMSIS-DAP
// engine receives a host command and `swd_bus_try_acquire(DAPLINK)`
// returns false, the engine MUST reply DAP_ERROR (busy) and the
// external host retries. Campaign and scanner never see "busy" —
// they're operator-driven and don't run concurrently in normal use.

#define SWD_BUS_TIMEOUT_NONE      0u
#define SWD_BUS_TIMEOUT_FOREVER   0xFFFFFFFFu

typedef enum {
    SWD_BUS_OWNER_IDLE     = 0,    // bus free
    SWD_BUS_OWNER_CAMPAIGN = 1,    // glitch_engine post-fire SWD verify (F9)
    SWD_BUS_OWNER_SCANNER  = 2,    // pinout_scanner during P(8,k) sweep (F8-2)
    SWD_BUS_OWNER_DAPLINK  = 3,    // CMSIS-DAP from external host (F7)
} swd_bus_owner_t;

// Reset to IDLE state. Safe to call repeatedly.
void swd_bus_lock_init(void);

// Try to claim the bus for `who`. `timeout_ms == SWD_BUS_TIMEOUT_NONE`
// returns immediately; `SWD_BUS_TIMEOUT_FOREVER` blocks until the
// current owner releases. Anything else polls every ~1 ms.
//
// Returns false if `who == SWD_BUS_OWNER_IDLE`, if the bus is held
// AND the timeout expires, or if the same owner already holds it
// (no re-entrance — surfacing accidental double-acquires is more
// useful than silently allowing them).
bool swd_bus_acquire(swd_bus_owner_t who, uint32_t timeout_ms);

// Convenience: non-blocking poll (timeout == 0).
bool swd_bus_try_acquire(swd_bus_owner_t who);

// Release the bus. Silent no-op if `who` doesn't match the current
// owner — surfaces the bug at the diagnostic / logging layer rather
// than crashing the campaign mid-fire. Caller is expected to check
// `swd_bus_owner()` before release if it's unsure.
void swd_bus_release(swd_bus_owner_t who);

// Current owner (or SWD_BUS_OWNER_IDLE if free). Lock-free read; the
// returned value may be stale by the time the caller acts on it,
// which is fine for diagnostics but NOT a substitute for actually
// calling `swd_bus_acquire`.
swd_bus_owner_t swd_bus_owner(void);

bool swd_bus_is_held(void);

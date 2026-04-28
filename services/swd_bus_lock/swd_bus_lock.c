/*
 * services/swd_bus_lock/swd_bus_lock.c — F9-1 cooperative mutex over
 * the shared SWD/JTAG scanner-header bus. See swd_bus_lock.h for the
 * contract.
 */

#include "swd_bus_lock.h"

#include "hal/time.h"

// Single-core cooperative model: volatile is sufficient. No IRQ-side
// acquires (F4/F5 PIO ISRs never touch SWD), so we don't need an
// atomic CAS or a pico-sdk mutex_t. Keeps host tests buildable
// against the plain hal_fake.
static volatile bool             s_locked = false;
static volatile swd_bus_owner_t  s_owner  = SWD_BUS_OWNER_IDLE;

void swd_bus_lock_init(void) {
    s_locked = false;
    s_owner  = SWD_BUS_OWNER_IDLE;
}

bool swd_bus_acquire(swd_bus_owner_t who, uint32_t timeout_ms) {
    if (who == SWD_BUS_OWNER_IDLE) {
        // "Acquire as idle" is meaningless — surface as a programmer
        // error rather than silently corrupting the owner field.
        return false;
    }

    uint32_t start = hal_now_ms();
    while (true) {
        if (!s_locked) {
            s_locked = true;
            s_owner  = who;
            return true;
        }
        // Bus held — same-owner double-acquire is rejected too (no
        // re-entrance). The campaign manager / scanner / daplink
        // each acquire-release in pairs around their critical
        // sections; if the same caller asks twice, that's a bug.
        if (timeout_ms == SWD_BUS_TIMEOUT_NONE) {
            return false;
        }
        if (timeout_ms != SWD_BUS_TIMEOUT_FOREVER) {
            uint32_t elapsed = (uint32_t)(hal_now_ms() - start);
            if (elapsed >= timeout_ms) return false;
        }
        // Yield once per millisecond. The bus is held by another
        // cooperative consumer in the same main loop, so sleeping
        // here keeps tud_task and the campaign ticks alive.
        hal_sleep_ms(1u);
    }
}

bool swd_bus_try_acquire(swd_bus_owner_t who) {
    return swd_bus_acquire(who, SWD_BUS_TIMEOUT_NONE);
}

void swd_bus_release(swd_bus_owner_t who) {
    if (s_owner != who) {
        // Wrong-owner release — silent no-op. The expected debug
        // path is for the caller to check swd_bus_owner() before
        // calling release if it's unsure; otherwise this catches
        // double-release without bringing down the system.
        return;
    }
    s_locked = false;
    s_owner  = SWD_BUS_OWNER_IDLE;
}

swd_bus_owner_t swd_bus_owner(void) {
    return s_owner;
}

bool swd_bus_is_held(void) {
    return s_locked;
}

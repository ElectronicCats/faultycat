#pragma once

#include <stdint.h>

// HAL — time
//
// Monotonic clocks + blocking sleep. Phase F1 scope.
// Async timers (alarm pools, callbacks) land when a service needs
// them; probably F3 for the USB task ticker.

// Block for `ms` milliseconds. `ms == 0` is a no-op by contract.
void hal_sleep_ms(uint32_t ms);

// Milliseconds since boot, monotonic. 32-bit counter wraps in ~49.7 days.
// Drivers that run longer than that must use a 64-bit absolute-time
// API when we expose one.
uint32_t hal_now_ms(void);

// Microseconds since boot, monotonic. 32-bit counter wraps in ~71.6
// minutes. Appropriate for short-window timing (trigger latency, pulse
// widths). Longer spans belong on hal_now_ms.
uint32_t hal_now_us(void);

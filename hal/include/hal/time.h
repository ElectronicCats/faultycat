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

// Tight busy-wait loop. Unlike hal_sleep_ms this does NOT yield: the
// CPU spins on the timer register. Use when timing accuracy matters
// more than power — e.g., sub-millisecond HV pulse widths.
void hal_busy_wait_us(uint32_t us);

// Disable all CPU interrupts and return an opaque cookie that
// `hal_irq_restore` accepts to reinstate the previous mask. Nestable
// via stacking the cookies. Wrap-around safe. Use in the tightest
// parts of HV pulse generation where a stray interrupt would
// stretch the pulse.
uint32_t hal_irq_save_and_disable(void);

// Restore the interrupt mask previously returned by
// hal_irq_save_and_disable.
void hal_irq_restore(uint32_t cookie);

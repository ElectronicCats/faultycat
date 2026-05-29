#pragma once

#include <stdbool.h>
#include <stdint.h>

// HAL — PWM
//
// Portable access to PWM generation. On RP2040 every GPIO pair has a
// dedicated PWM slice + channel; the HAL hides the slice/channel
// arithmetic and lets callers address PWM by pin number.
//
// Scope: frequency + duty-cycle configuration, enable/disable,
// query. Advanced features (phase correction, wrap IRQs, DMA
// pacing) land when a driver needs them.

typedef uint8_t hal_pwm_pin_t;

// Route `pin` to its PWM slice/channel. Does NOT start the PWM
// signal — the pin stays at the current level until enable.
void hal_pwm_init(hal_pwm_pin_t pin);

// Configure frequency (Hz) and duty (0.0 to 1.0). Takes effect
// immediately. Safe to call while the PWM is enabled. Frequency is
// approximated; the HAL picks the closest clkdiv/wrap combination.
void hal_pwm_set_freq_duty(hal_pwm_pin_t pin, float hz, float duty);

// Start producing the configured waveform on `pin`.
void hal_pwm_enable(hal_pwm_pin_t pin);

// Stop producing the waveform. The pin is left at whatever level it
// was holding when disabled — the driver (not the HAL) decides
// whether to drive it low after a safety-critical disable.
void hal_pwm_disable(hal_pwm_pin_t pin);

// True iff the PWM is currently enabled on `pin`.
bool hal_pwm_is_enabled(hal_pwm_pin_t pin);

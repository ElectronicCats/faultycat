#pragma once

#include <stdbool.h>
#include <stdint.h>

// drivers/hv_charger — ~250 V flyback high-voltage charger.
//
// *** HV DOMAIN *** This driver drives the flyback PWM on GP20 and
// reads the CHARGED feedback on GP18. When armed, the board builds
// up charge on a capacitor that can deliver dangerous shocks. The
// maintainer has committed to the safety-checklist gate in
// docs/SAFETY.md — every commit that modifies this driver must
// carry the signed checklist in the commit message body.
//
// Lifecycle
//   boot                 : DISARMED  (PWM disabled, GP20 low)
//   hv_charger_arm()     : ARMED     (PWM enabled, flyback pushes)
//   hv_charger_disarm()  : DISARMED  (PWM disabled, GP20 low)
//
// Auto-disarm
//   The driver keeps a millisecond timestamp of the last arm. If
//   hv_charger_tick() is called after the configured timeout
//   (default 60 000 ms / 60 s) without an intervening arm, the
//   driver disarms itself. Callers MUST tick regularly — at least
//   once per second — for the timeout to be honoured. Services that
//   busy-wait without ticking are responsible for calling the tick
//   themselves (e.g., before returning from a long operation).

typedef struct {
    // Flyback PWM frequency, Hz. Default: 2500 Hz (2.5 kHz) —
    // matches the legacy firmware/c/picoemp.c behaviour proven on
    // v2.x hardware.
    float flyback_freq_hz;

    // Flyback PWM duty, 0.0 to 1.0. Default: 0.0122 — also taken
    // from legacy. Higher duty → faster charging but hotter
    // transformer + MOSFET; don't raise without measuring first.
    float flyback_duty;

    // Auto-disarm timeout, milliseconds. 0 disables the timeout
    // (DANGEROUS — do not use outside of deliberately long HV
    // measurements and always with the shield installed). Default:
    // 60 000.
    uint32_t auto_disarm_ms;
} hv_charger_config_t;

// Configure the GPIOs + PWM. Leaves the charger DISARMED with GP20
// driven LOW. Applies the default config (2500 Hz, 0.0122 duty,
// 60 s timeout). Must be called before any other function.
void hv_charger_init(void);

// Replace the default config. Only takes effect on the next arm;
// a currently-armed session keeps running with the old values
// until it naturally disarms.
void hv_charger_configure(const hv_charger_config_t *cfg);

// Read the current config (the defaults if hv_charger_configure
// was never called).
void hv_charger_get_config(hv_charger_config_t *out);

// Start pushing charge. Idempotent — arming an already-armed
// charger resets the auto-disarm countdown.
void hv_charger_arm(void);

// Stop pushing charge immediately. Idempotent. Safe to call from
// any state.
void hv_charger_disarm(void);

// True iff the charger is armed (PWM enabled).
bool hv_charger_is_armed(void);

// True iff the CHARGED feedback on GP18 asserts — meaning the
// capacitor has reached its sense threshold. Active-low on the
// pin; this function returns the logical (normalized) state.
bool hv_charger_is_charged(void);

// Enforce the auto-disarm timeout. Must be called regularly while
// the charger is armed. Safe to call when disarmed (no-op).
void hv_charger_tick(void);

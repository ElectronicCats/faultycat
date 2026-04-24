#pragma once

#include <stdbool.h>
#include <stdint.h>

// drivers/emfi_pulse — GP14 HV pulse output for electromagnetic
// fault injection.
//
// *** HV DOMAIN *** GP14 drives the HV pulse MOSFET. When the
// capacitor (driven by drivers/hv_charger) is charged, raising this
// pin dumps the cap through the EMFI coil on the SMA output. The
// plastic shield and a mounted coil are MANDATORY — see docs/SAFETY.md.
//
// F2b scope — CPU-timed manual fire only
//   Ports firmware/c/picoemp.c::picoemp_pulse verbatim: disable
//   interrupts, drive GP14 high, busy-wait, drive low, restore
//   interrupts. Simple, deterministic, proven on v2.x. This is
//   enough for visual scope validation with an EMFI coil absorbing
//   the pulse.
//
//   Sub-µs triggered fire via PIO is deferred to F4 (glitch_engine
//   service), where it integrates with the campaign manager, the
//   trigger compiler, and the ADC capture ring. hal/pio.h stays a
//   #error stub until then.
//
// Intended fire protocol (enforced by the CALLER, not this driver):
//   1. hv_charger_arm() + wait until hv_charger_is_charged() == true
//   2. emfi_pulse_fire_manual(width_us)
//   3. hv_charger_disarm()   — cap is largely spent but enforce anyway
//
// The driver does NOT check HV state before firing. This keeps the
// layering clean (driver has no policy) but ALSO means a caller
// that skips step 1 will drive GP14 high with an empty cap, which
// is a no-op electrically but masks bugs. Always compose with
// hv_charger at the app or service layer.

// Minimum and maximum pulse widths, hard-coded safety rails. Outside
// this range, emfi_pulse_fire_manual does nothing and returns false.
#define EMFI_PULSE_MIN_WIDTH_US 1u
#define EMFI_PULSE_MAX_WIDTH_US 50u

// Post-fire cool-down. The legacy firmware forces 250 ms of dead
// time after each pulse to let the cap fully bleed and the
// operator's neurons catch up. Exposed so tests can assert it.
#define EMFI_PULSE_COOLDOWN_MS  250u

// Configure GP14 as a plain GPIO output driven LOW. Must be called
// before any fire. Safe to call repeatedly.
void emfi_pulse_init(void);

// Disable the HV pulse output: drive GP14 low and leave it there.
// Called at init and after every fire; also exposed so an upper
// layer can force a known-safe state from a fault path.
void emfi_pulse_force_low(void);

// Emit a single HV pulse of `width_us` microseconds, using the CPU
// to time the pulse while interrupts are disabled. Enforces the
// min/max safety rails and the post-fire cool-down. Returns true
// if the pulse was emitted, false if the width was rejected.
bool emfi_pulse_fire_manual(uint32_t width_us);

// Forward decl — the opaque HAL PIO handle.
typedef struct hal_pio_inst hal_pio_inst_t;

// Hand GP14 over to PIO. While attached, emfi_pulse_fire_manual
// refuses to fire (returns false) so the CPU path and the PIO path
// never contend for the pin. The service layer (F4-3/F4-5) is
// responsible for the pio_gpio_init + pindir setup before this call.
//
// SAFETY: a caller that attaches while the HV charger is armed
// transfers control of the HV pulse MOSFET to the PIO program —
// make sure the PIO program holds GP14 LOW until the trigger/delay
// phase completes. docs/SAFETY.md §3 #5 invariant fires in F4-5.
bool emfi_pulse_attach_pio(hal_pio_inst_t *pio, uint32_t sm);

// Return GP14 to plain-GPIO ownership driven LOW. Safe from any
// state. No-op if not attached.
void emfi_pulse_detach_pio(void);

// True iff GP14 currently belongs to PIO.
bool emfi_pulse_is_attached_to_pio(void);

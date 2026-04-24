# FaultyCat v3 — HV safety procedures

This document defines the **commit-level safety gate** for any code
that directly drives the ~250 V flyback capacitor or the EMFI HV
pulse path, plus the physical-test procedures operators must follow
when handling an armed FaultyCat.

## 1. Commit-level safety gate

Every commit that modifies any of the following paths MUST include
the **signed safety checklist** below in the commit message body:

- `drivers/hv_charger/`
- `drivers/emfi_pulse/`
- `hal/src/rp2040/pwm.c`, `hal/src/rp2040/pio.c`  (when the change
  exists to support an HV driver)

`drivers/crowbar_mosfet/` is exempt from the mandatory gate because
the driver alone drives only gate GPIOs with no HV present. However,
any commit that changes it in a way that could alter the
break-before-make invariant SHOULD still attach the checklist.

### Signed checklist

Paste this block, fill each `[ ]` with `y`, `n`, or `NA`, and sign.
Every `n` or `NA` must be followed by a `Safety-justification:` line
on the next line.

```
Safety: HV charger is in DISARMED state at firmware boot. [ ]
Safety: 60-second auto-disarm is active by default and tested. [ ]
Safety: plastic shield is installed for any physical test of this change. [ ]
Safety: operator has a known-good GND reference. [ ]
Safety: the output SMA is either loaded by an EMFI coil or discharged. [ ]
Safety: oscilloscope probe is rated for the measured voltage (10x / 100x / differential). [ ]
Safety: break-before-make on MOSFET gate transitions is preserved by this change. [ ]
Safety: build, flash, and verify were performed in person by the maintainer. [ ]
Signed-off-by: <Name> <email>
```

### Example

```
Safety: HV charger is in DISARMED state at firmware boot. [y]
Safety: 60-second auto-disarm is active by default and tested. [y]
Safety: plastic shield is installed for any physical test of this change. [y]
Safety: operator has a known-good GND reference. [y]
Safety: the output SMA is loaded by an EMFI coil. [y]
Safety: oscilloscope probe is rated for the measured voltage (10x / 100x / differential). [y]
Safety: break-before-make on MOSFET gate transitions is preserved by this change. [NA]
Safety-justification: this commit only adds hv_charger; crowbar path unchanged.
Safety: build, flash, and verify were performed in person by the maintainer. [y]
Signed-off-by: Sabas <sabasjimenez@gmail.com>
```

A commit that lacks this block, or that has unjustified `n`/`NA`
entries, MUST NOT be tagged or pushed to a shared branch.

## 2. Physical operating procedure

1. **Shield installed.** The plastic shield over the HV cap is
   mandatory during any arm/fire sequence. The original README's
   warning stands: "you will still easily shock yourself on the
   exposed high-voltage capacitor."
2. **SMA loaded or discharged.** Never arm the HV with an open SMA
   connector. Either mount an EMFI injection coil (absorbs the
   pulse) or discharge the SMA manually to a known GND point.
3. **Auto-disarm trust.** The 60-second auto-disarm in
   `drivers/hv_charger` is a **safety net**, not a design control.
   Always disarm explicitly from software or the PULSE button the
   moment you are done measuring.
4. **Oscilloscope probes.**
   * Reading the cap voltage directly requires a 100× probe or a
     differential probe rated for >300 V — never use a 1× probe.
   * Reading the EMFI pulse on the SMA side with the coil mounted is
     low-voltage (sub-10 V across the coil) and 1×/10× is fine.
   * Reading GP20 (HV PWM drive) is 0–3.3 V, 1×/10× is fine.
5. **GND reference.** Share GND between the scope, the board, and
   any target. Do not float the board on a battery while probing
   with a mains-powered scope unless using a true differential probe.
6. **Post-session discharge.** Before touching the board after HV
   work, disarm, wait ≥10 seconds for the internal bleed path to
   drop the cap, and verify with a multimeter across the cap
   terminals.

## 3. Driver-level safety invariants

The following invariants are asserted by the drivers and exercised
by the host tests. Breaking any of them is a safety bug.

1. `hv_charger_init()` leaves the charger in the DISARMED state
   with `BOARD_GP_HV_PWM` driven LOW as a plain GPIO.
2. The 60-second auto-disarm default is applied by `hv_charger_init`
   (configurable only via `hv_charger_configure`; never zero by
   accident — callers must explicitly opt in with `auto_disarm_ms =
   0` and a corresponding `Safety-justification` line).
3. `crowbar_mosfet` guarantees break-before-make: no steady state
   has both LP (GP16) and HP (GP17) gates HIGH simultaneously.
4. The glitch-fire path in `services/glitch_engine` (F5) takes
   ownership of the MOSFET gate for the pulse duration only. Before
   and after the fire window, `crowbar_mosfet` owns the gate state.
5. `emfi_pulse` (F2b-4) will require (a) `hv_charger_is_charged()`
   to have been true at least once in the last 100 ms, and (b) an
   explicit arm-token issued by the app layer. This invariant is
   planned for F2b-4 — the checklist item for it gets activated in
   that commit.

## 4. What to do when you break an invariant

A commit landed that violated this document. Options in order of
preference:

1. Revert the commit and open a discussion before re-landing.
2. Land a follow-up commit that restores the invariant AND documents
   the episode in a new subsection of this file, so the next
   maintainer has the context.

Do NOT edit away the mention in git history. Transparency is part
of the safety protocol.

# FaultyCat HW v2.x — pinout reference

> Firmware v3.0 targets this hardware. **Production version shipped is
> v2.2** (per maintainer Sabas, 2026-04-23); v2.1 → v2.2 changed only
> silkscreen labels, not nets. All pin assignments below apply to both.

Authoritative sources used for this map:

- `firmware/c/board_config.h` (the current v2.x firmware, proven in the
  field).
- `firmware/c/glitcher/glitcher.c` (for `GlitchOutput_{LP,HP,EMP}` →
  GPIO mapping, with legacy-quirk annotations).
- `Hardware/FaultyCat.kicad_sch` (net labels cross-referenced).

## 1. GPIO → function map (RP2040 QFN-56)

| GPIO | Schematic net   | Function                         | v3 driver         | Notes |
|------|-----------------|----------------------------------|-------------------|-------|
| GP0  | GP0             | Scanner CH0                      | `scanner_io`      | |
| GP1  | GP1             | Scanner CH1                      | `scanner_io`      | legacy `main.c` drove GP1 as target-RESET — that's policy, not pinout; moves to `services/` |
| GP2  | GP2             | Scanner CH2                      | `scanner_io`      | |
| GP3  | GP3             | Scanner CH3                      | `scanner_io`      | |
| GP4  | GP4             | Scanner CH4                      | `scanner_io`      | |
| GP5  | GP5             | Scanner CH5                      | `scanner_io`      | |
| GP6  | GP6             | Scanner CH6                      | `scanner_io`      | |
| GP7  | GP7             | Scanner CH7                      | `scanner_io`      | last of the 8 scanner channels |
| GP8  | TRIGGER_IN      | External trigger in (v2.1+)      | `ext_trigger`     | level-shifted via TRIGGER_VREF |
| GP9  | —               | LED "HV DETECTED"                | `ui_leds`         | lights while the HV capacitor is charged — driven by the `CHARGED` feedback (GP18) with a 500 ms hold to avoid flicker. Legacy `board_config.h` mis-names it `PIN_LED_HV_ARMED`; v3 renames to reflect actual behaviour. Physically confirmed on v2.2, 2026-04-23. |
| GP10 | —               | LED "STATUS"                     | `ui_leds`         | **NOT** a scanner channel. Used by the F0 blink. Physically confirmed on v2.2, 2026-04-23. |
| GP11 | —               | BTN "PULSE"                      | `ui_buttons`      | active-low, pullup + input-invert |
| GP14 | HVPULSE         | EMFI HV pulse out (PIO-driven)   | `emfi_pulse`      | **HV domain — see §3** |
| GP16 | LPGLITCH2       | Crowbar low-power path           | `crowbar_mosfet`  | |
| GP17 | HPGLITCH2       | Crowbar N-MOSFET gate (IRLML0060)| `crowbar_mosfet`  | **the real voltage glitch path** |
| GP18 | CHARGED         | HV feedback "charged" (act-low)  | `hv_charger`      | **HV domain** |
| GP20 | HVPWM           | HV flyback PWM ~2.5 kHz          | `hv_charger`      | **HV domain** |
| GP25 | —               | NOT CONNECTED on v2.x            | —                 | legacy `board_config.h` has `PIN_LED_STATUS = 25` — that is a Pico-module relic; the v2.x PCB wires RP2040 directly and GP25 goes nowhere. Do not use. |
| GP27 | —               | LED "CHARGE ON"                  | `ui_leds`         | on while the flyback PWM on GP20 is actively charging. Physically confirmed on v2.2, 2026-04-23. See §4 quirk. |
| GP28 | —               | BTN "ARM"                        | `ui_buttons`      | active-high, pulldown |
| GP29 | ANALOG (ADC3)   | Target monitor analog in (v2.1+) | `target_monitor`  | |

GPIOs not listed (GP12, GP13, GP15, GP19, GP21, GP22, GP23, GP24, GP26)
are not connected to meaningful nets on v2.x and are reserved for
future use.

## 2. Scanner header — `Conn_01x10`

- Footprint: `Connector_PinSocket_2.54mm:PinSocket_1x10_P2.54mm_Vertical`.
- **8 signal pins** (GP0–GP7) + **VCC** + **GND** = 10 pins total.
- The JTAGulator-style algorithm (blueTag, F8) enumerates every
  permutation of the 8 channels to auto-discover JTAG and SWD pins on
  the target.
- **SWD over the scanner header (F6).** v2.x has no dedicated SWD
  header for `services/swd_core` to use. F6 routes SWD over two of
  the eight scanner channels — defaults `BOARD_GP_SWCLK_DEFAULT`
  = CH0 (GP0), `BOARD_GP_SWDIO_DEFAULT` = CH1 (GP1),
  `BOARD_GP_SWRST_DEFAULT` = CH2 (GP2). The CDC2 shell command
  `swd init <swclk> <swdio> [<nrst>]` re-pins them at runtime.
- **TXS0108EPW level shifter on the scanner header — known issue.**
  All 8 scanner channels pass through a TI TXS0108EPW
  auto-direction level shifter (OE permanently pulled high via 4.7 kΩ
  to VREF). The TXS0108E is documented to misbehave with push-pull
  bidirectional protocols where both endpoints actively drive the
  same line at different times — the chip's one-shot accelerator on
  the host-release side fights the target's drive during the SWD
  ACK window, so the target's LOW gets clamped HIGH before the host
  PIO samples it. Symptom: every SWD transaction returns
  `ACK = 0b111 = NO_TARGET` even though SWCLK and SWDIO toggle
  cleanly on the wire.
  - Verified by flashing canonical raspberrypi/debugprobe firmware
    onto FaultyCat (GP0=SWCLK, GP1=SWDIO) and running OpenOCD
    against an external Pi Pico target — also fails with
    "Failed to connect multidrop rp2040.dap0", confirming the bug
    is HW-path, not in our F6 implementation.
  - **F6 software workaround**: `services/swd_core/swd_phy.c`
    emulates open-drain on SWDIO — the PIO bitloop writes pin
    *direction* instead of pin *value* (release for HIGH, drive
    for LOW). The chip handles open-drain bidirectional cleanly
    because only one side ever sources current at a time.
  - **Future board revs** should swap the TXS0108E for either
    direct-bypass (both ends at 3.3 V already, level shifter is
    redundant) or 74LVC1T45 with explicit DIR control.
- **Mutual exclusion contract (F6 → F9).** `drivers/scanner_io`,
  `services/swd_core`, `services/jtag_core` (F8), and
  `services/pinout_scanner` (F8) all share GP0..GP7 — only one
  may own a given pin at a time. F6 documents the contract; F9
  lands the formal `pico-sdk mutex_t`-based lock with priority
  `campaign > scanner > daplink_host`.

## 3. HV domain — safety

GP14 (`HVPULSE`), GP18 (`CHARGED`), GP20 (`HVPWM`), plus the `VREF`
tree, touch the ~250 V flyback-cap output stage. **Rules enforced by
this project:**

1. Any commit that modifies `drivers/hv_charger/`, `drivers/emfi_pulse/`,
   or `drivers/crowbar_mosfet/` MUST include an explicit safety
   checklist signed in the commit body by the maintainer.
2. The plastic shield must be installed before any HV test. Never
   touch the SMA output or the exposed HV capacitor while armed.
3. The 60-second auto-disarm in the legacy firmware carries over to v3
   as a **driver-level safety** (in `drivers/hv_charger/`), not a
   policy knob — disabling it requires a build flag, not a runtime
   command.

(Full SAFETY.md lives beside this file and is updated in F2 when the
HV driver lands.)

## 4. Known legacy quirks (won't be ported as-is)

- `firmware/c/glitcher/glitcher.c` defines `PIN_EXT1 = 27`, which
  **collides** with `PIN_LED_CHARGE_ON = 27`. On v2.x, GP27 is the
  charge-on LED (confirmed by the maintainer on 2026-04-23). The
  legacy constant is a stale upstream faultier relic; v3 does not port
  `PIN_EXT1` at all. Crowbar uses GP17 (`HPGLITCH2`).
- `firmware/c/board_config.h` has `PIN_LED_STATUS = 25`, but on v2.x
  **GP25 is not connected to anything** — the Pico-module assumption
  is stale (FaultyCat v2.x uses bare RP2040). The real STATUS LED is
  on GP10. Confirmed by flashing the F0 blink on both pins against a
  physical v2.2 board on 2026-04-23 — GP25 was dark, GP10 blinks.
- `firmware/c/picoemp.c` unrolls `#undef` macro hacks to rename pins
  from `board_config.h` — those disappear in v3 because each driver
  owns its pin set directly.
- The legacy `main.c` calls `gpio_init(1); gpio_set_dir(1, GPIO_OUT);
  gpio_put(1, 1);` inside `main()` — this is an undocumented
  target-RESET assertion. v3 models it as `drivers/scanner_io/` pin
  behaviour driven by an explicit `pinout_scanner` call, not a
  hard-coded init.

## 5. LEDs summary (the full set on v2.2)

Only **three** LEDs exist on the FaultyCat v2.2 physical board — all
three confirmed by the maintainer on 2026-04-23 by flashing F0 test
blinks:

| GPIO | Silk / function | When it lights |
|------|-----------------|----------------|
| GP9  | HV DETECTED     | capacitor is charged to HV threshold (hysteresis 500 ms). |
| GP10 | STATUS          | free-form status indicator; F0 blink uses this. |
| GP27 | CHARGE ON       | flyback PWM is actively pushing into the capacitor. |

The legacy `firmware/c/board_config.h` references a `PIN_LED_STATUS =
25` that does **not** exist on the v2.x PCB (see §4). v3 drivers will
not initialise GP25 for any purpose.

## 6. Confirmations open

None for HW v2.x pinout as of 2026-04-23. This document is **closed for
F0**; future amendments ride along with the driver work in F2.

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
| GP9  | —               | LED "HV ARMED"                   | `ui_leds`         | |
| GP10 | —               | LED "STATUS"                     | `ui_leds`         | **NOT** a scanner channel (2026-04-23 confirmed) |
| GP11 | —               | BTN "PULSE"                      | `ui_buttons`      | active-low, pullup + input-invert |
| GP14 | HVPULSE         | EMFI HV pulse out (PIO-driven)   | `emfi_pulse`      | **HV domain — see §3** |
| GP16 | LPGLITCH2       | Crowbar low-power path           | `crowbar_mosfet`  | |
| GP17 | HPGLITCH2       | Crowbar N-MOSFET gate (IRLML0060)| `crowbar_mosfet`  | **the real voltage glitch path** |
| GP18 | CHARGED         | HV feedback "charged" (act-low)  | `hv_charger`      | **HV domain** |
| GP20 | HVPWM           | HV flyback PWM ~2.5 kHz          | `hv_charger`      | **HV domain** |
| GP25 | —               | LED onboard (status)             | `ui_leds`         | used by F0 blink |
| GP27 | —               | LED "CHARGE ON"                  | `ui_leds`         | see §4 quirk |
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
- `firmware/c/picoemp.c` unrolls `#undef` macro hacks to rename pins
  from `board_config.h` — those disappear in v3 because each driver
  owns its pin set directly.
- The legacy `main.c` calls `gpio_init(1); gpio_set_dir(1, GPIO_OUT);
  gpio_put(1, 1);` inside `main()` — this is an undocumented
  target-RESET assertion. v3 models it as `drivers/scanner_io/` pin
  behaviour driven by an explicit `pinout_scanner` call, not a
  hard-coded init.

## 5. Confirmations open

None for HW v2.x pinout as of 2026-04-23. This document is **closed for
F0**; future amendments ride along with the driver work in F2.

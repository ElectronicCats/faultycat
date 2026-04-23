# Porting analysis — legacy `firmware/c` → v3

## Summary

The current `firmware/c` tree is our single best reference for the
v2.x hardware because it is *already talking to this board*. We do
**not** merge it into v3 wholesale — the rewrite is architectural, not
a dust-off. This doc is the contract for *what* moves, *how*, and
*why*.

## Module-by-module disposition

| Legacy file / path                                            | v3 decision        | v3 destination                              | Rationale |
|---------------------------------------------------------------|--------------------|---------------------------------------------|-----------|
| `firmware/c/main.c`                                           | Rewrite            | `apps/faultycat_fw/main.c`                  | The legacy `main` is a single monolithic switch on a multicore FIFO — replaced by a services/USB dispatcher. |
| `firmware/c/picoemp.c` + `picoemp.h` (HV charger + EMFI pulse)| Port, split in two | `drivers/hv_charger/` + `drivers/emfi_pulse/` | Proven HV flyback and pulse-gen logic; split cleanly along responsibility. |
| `firmware/c/board_config.h`                                   | Reference, supersede | `docs/HARDWARE_V2.md` + per-driver pin consts | `board_config.h` was the source of truth; now each driver owns its pin set and `HARDWARE_V2.md` is the authoritative map. |
| `firmware/c/trigger.pio`, `trigger_basic.pio`                 | Port, tidy         | `drivers/emfi_pulse/trigger.pio`            | PIO program for fast-trigger works; clean up the naming. |
| `firmware/c/glitcher/glitcher.c`                              | **Rewrite**        | `services/glitch_engine/crowbar/`           | Heavy reliance on `faultier`-derived sources that are not legally portable (see §Legal). Reimplemented against the same HW v2.1 crowbar path. |
| `firmware/c/glitcher/glitcher_commands.c`                     | Discard            | —                                           | Ad-hoc serial-command glue; replaced by `host_proto/crowbar_proto/`. |
| `firmware/c/faultier/glitcher/ft_pio.{c,h}`                   | **Do not port**    | —                                           | `hextreeio/faultier` has no LICENSE. Pattern reimplemented from scratch. |
| `firmware/c/faultier/glitcher/{trigger,delay,glitch}_compiler.c` | **Do not port** | —                                           | Same. |
| `firmware/c/faultier/glitcher/power_cycler.c`                 | **Do not port**    | —                                           | Same. |
| `firmware/c/faultier/swd/tamarin_probe.c`, `probe.pio`        | **Do not port**    | —                                           | Same, and `debugprobe` (MIT) gives us SWD with a clean license. |
| `firmware/c/faultier/proto/*` (nanopb generated)              | Discard            | —                                           | v3 uses binary protocols directly over CDC + standard CMSIS-DAP — no nanopb. |
| `firmware/c/serial/serial.c` (text console, command registry) | Partial rewrite    | `services/pinout_scanner/shell.c` (text shell only) | The text-console UX is useful for the scanner menu (blueTag-like); the per-command binary protocols move to `host_proto/*`. |
| `firmware/c/blueTag/` (submodule, v1.0.2)                     | Upgrade + port     | `third_party/blueTag/` (v2.1.2) + `services/{pinout_scanner, buspirate_compat, flashrom_serprog}` | MIT; brings JTAGulator + BusPirate-emulation for OpenOCD + flashrom serprog for free. |
| `firmware/c/blueTag.h`, `jep106.inc`                          | Regenerate in F8   | in-tree under `services/pinout_scanner/`    | Generated from the submodule. |

## Legal / compliance — the `faultier` question

Verified 2026-04-23 against the GitHub REST API: `hextreeio/faultier`
has the `license` field set to `null` — there is no `LICENSE`,
`LICENSE.md`, `LICENSE.txt`, or `COPYING` file in the repo. Under the
default copyright regime this is **"all rights reserved."**

Consequence for v3:

- No file in v3 is copied, adapted, or translated from `faultier`.
- `third_party/faultier/` stays in the tree, pinned to commit
  `1c78f3e`, **as reference-only reading material**. `CMakeLists.txt`
  explicitly refuses to `add_subdirectory` it.
- Where `faultier`'s architecture influenced ours (compiling trigger/
  delay/glitch/power-cycle steps into a PIO program), we credit the
  concept here and reimplement from scratch under BSD-3-Clause.
- Should upstream ever adopt a permissive license, this stance is
  revisited file-by-file in a follow-up PR.

## What we port 1:1 because it is HW-proven

- `picoemp_enable_pwm(duty)` — the 2.5 kHz flyback PWM routine, with
  its duty-cycle default of `0.0122`.
- The `PIN_LED_HV` hysteresis with a 500 ms hold to avoid LED flicker
  during rapid charge/discharge cycles.
- Button wiring conventions: ARM active-high with pulldown, PULSE
  active-low with pullup + input-invert (gives identical read semantics
  at the API).
- The 60 s auto-disarm in the armed state (safety default — not a
  runtime toggle).

## What we explicitly abandon

- `multicore_fifo` control plane (core1 running the serial console).
  v3 is single-core cooperative with a TinyUSB task loop.
- Nanopb encoding. Custom binary protocols over CDC + standard
  CMSIS-DAP are simpler and decouple us from a codegen dependency.
- `stdio_usb` redirection of printf to the sole CDC. v3's CDCs are
  typed by purpose; diagnostic logging moves to a debug CDC or to
  target-UART.
- `PIN_*` macro rewrap via `#undef` in `picoemp.c`. v3 drivers consume
  pin constants directly without macro collision workarounds.

## What we correct while porting

- Drop `PIN_EXT1 = 27` (collides with the charge-on LED on real v2.x).
- Collapse the `GlitchOutput_None / LP / HP / EMP` enum into two
  orthogonal things: *which path* (EMFI vs crowbar — decides the CDC
  and service) and *which physical output* (the driver picks based on
  configuration). This makes the new separation-by-attack-type rule
  (decision #14) legible.

## Timeline

The modules in this table move as their owning phase lands (F2 for
drivers, F4 for EMFI service, F5 for crowbar service, F8 for scanner
+ BusPirate + serprog). This doc is updated in each phase's commit to
tick the row.

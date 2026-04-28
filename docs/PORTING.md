# Porting analysis — legacy `firmware/c` → v3

## Summary

The current `firmware/c` tree is our single best reference for the
v2.x hardware because it is *already talking to this board*. We do
**not** merge it into v3 wholesale — the rewrite is architectural, not
a dust-off. This doc is the contract for *what* moves, *how*, and
*why*.

## Module-by-module disposition

Status column: `[x] done` / `[~] partial` / `[ ] pending`. Updated at
every phase close.

| # | Legacy file / path                                            | v3 decision        | v3 destination                              | Status | Rationale |
|---|---------------------------------------------------------------|--------------------|---------------------------------------------|--------|-----------|
| 1 | `firmware/c/main.c`                                           | Rewrite            | `apps/faultycat_fw/main.c`                  | [x] F3-4 — diag now writes to the composite's CDC2 scanner, no more stdio_usb | Legacy `main` was a monolithic switch on a multicore FIFO; replaced by a single-core dispatcher that calls usb_composite_task + driver state machines. Full service decomposition lands with F4/F5. |
| 2 | `firmware/c/picoemp.c` + `picoemp.h` (HV charger)             | Port, split        | `drivers/hv_charger/`                       | [x] F2b (SIGNED commit `f450d43`) | Flyback PWM 2.5 kHz + 0.0122 duty + 60 s auto-disarm + CHARGED polarity normalization. Invariants tested; physically verified. |
| 3 | `firmware/c/picoemp.c::picoemp_pulse` (EMFI manual fire)      | Port               | `drivers/emfi_pulse/`                       | [x] F2b (SIGNED commit `69792ac`) | CPU-timed pulse with interrupts disabled, ported verbatim. Safety-railed [1..50] µs; 250 ms cool-down; auto-disarm after every fire. |
| 4 | `firmware/c/picoemp.c::picoemp_process_charging` (LED hysteresis) | Port            | `drivers/ui_leds::ui_leds_hv_detected_feed` | [x] F2a (commit `33891e1`) | 500 ms hold ported exactly. HV_DETECTED LED now lights via `hv_charger_is_charged()` piped through the hysteresis. |
| 5 | `firmware/c/board_config.h`                                   | Reference, supersede | `drivers/include/board_v2.h` + `docs/HARDWARE_V2.md` | [x] F2a | v3 single-source pinout. `PIN_LED_STATUS=25` Pico-relic dropped; GP10 is the real STATUS. Legacy `PIN_EXT1=27` collision with charge LED dropped. |
| 6 | `firmware/c/trigger.pio`, `trigger_basic.pio`                 | Port, tidy         | `drivers/emfi_pulse/trigger.pio` → now `services/glitch_engine/emfi/emfi_pio.c` | [x] F4-3 — PIO trigger compiler reimplemented from scratch in emfi_pio.c (hand-authored opcodes). | Faultier-inspired arch (trigger block + delay + pulse linearised into one SM) but zero lines copied from unlicensed upstream. |
| 7 | `firmware/c/glitcher/glitcher.c`                              | **Rewrite**        | `services/glitch_engine/crowbar/`           | [x] F5 — `crowbar_pio` (F5-2, pio0/SM1, IRQ 1) + `crowbar_campaign` (F5-3) reimplemented from scratch under BSD-3 | Legacy is a direct port of `faultier` internals, which are not legally portable. Reimplemented from scratch against the crowbar HW path. |
| 8 | `firmware/c/glitcher/glitcher_commands.c`                     | Discard            | —                                           | [x] discarded | Replaced by `host_proto/crowbar_proto/` (F5) on CDC1. |
| 9 | `firmware/c/faultier/glitcher/ft_pio.{c,h}`                   | **Do not port**    | —                                           | [x] not ported (policy) | `hextreeio/faultier` has no LICENSE. Pattern reimplemented from scratch in F4/F5. — F4-3 service architecture mirrors the compiler shape (trigger block → delay → glitch) without the code. |
| 10 | `firmware/c/faultier/glitcher/{trigger,delay,glitch}_compiler.c` | **Do not port** | —                                         | [x] not ported (policy) | Same. — F4-3 service architecture mirrors the compiler shape (trigger block → delay → glitch) without the code. |
| 11 | `firmware/c/faultier/glitcher/power_cycler.c`                 | **Do not port**    | —                                           | [x] not ported (policy) | Same. |
| 12 | `firmware/c/faultier/swd/tamarin_probe.c`, `probe.pio`        | **Do not port**    | —                                           | [x] not ported (policy) | Same. F6 takes the SWD core from `third_party/debugprobe` (MIT) instead — see new row #19. |
| 13 | `firmware/c/faultier/proto/*` (nanopb generated)              | Discard            | —                                           | [x] discarded | v3 uses binary protocols directly over CDC + standard CMSIS-DAP — no nanopb. |
| 14 | `firmware/c/serial/serial.c` (text console, command registry) | Partial rewrite    | `apps/faultycat_fw/main.c::process_shell_line` (per-service subshells: swd / jtag / scan / buspirate / serprog) | [x] F8-3 | Shell ended up in `apps/` rather than `services/pinout_scanner/shell.c` (the original plan filename was a shorthand). The unified parser dispatches by prefix into per-service handlers; output prefixes (`SWD:` / `JTAG:` / `SCAN:` / `SHELL:` / `BPIRATE:` / `SERPROG:`) keep `tools/{swd,jtag,scanner}_diag.py` decoupled. Mode-switch placeholders (`buspirate enter`, `serprog enter`) emit a stable error today; F8-4 / F8-5 swap them for binary-mode pumps. Per-command binary protocols still move to `host_proto/*` per the original plan. |
| 15 | `firmware/c/blueTag/` (submodule, v1.0.2)                     | Upgrade + port     | `third_party/blueTag/` (v2.1.2) + `services/{pinout_scanner, buspirate_compat, flashrom_serprog}` | [~] submodule upgraded to v2.1.2 (F0); services land in F8 | MIT; brings JTAGulator + BusPirate-emulation for OpenOCD + flashrom serprog for free. |
| 16 | `firmware/c/blueTag.h`, `jep106.inc`                          | Regenerate in F8   | in-tree under `services/pinout_scanner/`    | [ ] F8 | Generated from the submodule. |
| 17 | Legacy button wiring (ARM pulldown + PULSE pullup + input-invert) | Port (polarity normalized in software, not `gpio_set_inover`) | `drivers/ui_buttons/` | [x] F2a | Same UX; HAL stays portable. |
| 18 | Legacy crowbar LP/HP gate selection (GP16/GP17)               | Port, reshape      | `drivers/crowbar_mosfet/` + F5 service      | [x] F5 — driver kept (F2b, break-before-make); policy in `crowbar_campaign` enforces NONE at arm/fire/teardown so the gate hand-off to PIO is observable | Driver has no policy. |
| 19 | `third_party/debugprobe/src/probe.{c,pio}` (PROBE_IO_RAW)     | Port + adapt       | `services/swd_core/swd_phy.{c,h}` + hand-encoded PIO opcodes | [x] F6-2 SIGNED-not-required — MIT, attribution at file head. Runtime SWCLK/SWDIO/nRST pins (v2.x has no dedicated SWD header; defaults to scanner CH0/CH1/CH2). pio1/SM0. No FreeRTOS port (probe.c was self-contained) | The wire layer above (swd_dp + swd_mem) was **rewritten from scratch** under BSD-3 against ARM IHI 0031, not derived from any GPL-incompatible source. |
| 19 | `firmware/c/glitcher/glitcher.c::prepare_adc`                 | Port (HW-proven)   | `services/glitch_engine/emfi/emfi_capture.c` | [x] F4-4 | FaultyCat-origin BSD-3 code. 8192-byte ring, 8-bit shift, DREQ_ADC preserved exactly. |
| 20 | `third_party/blueTag/src/blueTag.c::{jtagConfig, restoreIdle, enterShiftDR/IR, getDeviceIDs, detectDevices, isValidDeviceID, calculateJtagPermutations, bitReverse}` | Port (function shapes) | `services/jtag_core/jtag_core.{c,h}` | [x] F8-1 | blueTag MIT, attribution at jtag_core.c head. Reimplemented against `hal/gpio` so the v3 layered model holds (no `pico/stdlib.h` reach-around). Pin uniqueness validation added; TDO pull-up wired; chain-detect/IDCODE flow combined behind a single `jtag_read_idcodes` entry point. CPU bit-bang chosen over PIO — TXS0108E caps wire rate anyway and the host-side test path stays trivial. |
| 21 | `third_party/blueTag/src/blueTag.c::{jtagScan, swdScan, calculateJtagPermutations}` | Port (algorithm only) | `services/pinout_scanner/pinout_scanner.{c,h}` | [x] F8-2 | blueTag MIT (same attribution chain as row 20). Brute-force JTAG and SWD pinout discovery rewritten as a pure k-permutation iterator + a candidate loop that calls F8-1's jtag_core / F6's swd_dp under the hood. First-match semantics preserved. Cooperative yield hook (`pinout_scanner_progress_cb`) added so a 1680-iteration scan doesn't starve `tud_task` or the F4/F5 glitch campaigns. blueTag's hard-coded 16-channel limit drops to a runtime `n` parameter so the iterator runs over v2.x's 8 channels without modification (and remains forward-compatible with future board revs). |
| 22 | `third_party/blueTag/src/modules/openocd/openocdJTAG.c::{openocdModeJTAG, tapShiftRain}` + `openocdHandler.h::CMD_*` | Port (protocol + algorithm) | `services/buspirate_compat/buspirate_compat.{c,h}` | [x] F8-4 | blueTag MIT (attribution at buspirate_compat.c head). Reworked from upstream's blocking `getc(stdin)` + 4 KB-buffered TAP_SHIFT into a streaming 14-state machine (`buspirate_compat_feed_byte`) so the cooperative main loop never stalls. Callback-based JTAG clocker so tests stub both the byte sink and the bit primitive without simulating a PIO. Implements: BBIO entry (0x00→BBIO1, 0x06→OCD1, 0x0F→user-term/exit), OOCD CMD_UNKNOWN / PORT_MODE / FEATURE / READ_ADCS / TAP_SHIFT / ENTER_OOCD / UART_SPEED / JTAG_SPEED. Length clamp (0x2000) inherited from upstream. |

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

- [x] `picoemp_enable_pwm(duty)` — the 2.5 kHz flyback PWM routine,
  `0.0122` duty default. In `drivers/hv_charger/` (F2b).
- [x] `PIN_LED_HV` hysteresis with 500 ms hold. In
  `drivers/ui_leds::ui_leds_hv_detected_feed` (F2a).
- [x] Button wiring: ARM active-high pulldown, PULSE active-low
  pullup. Polarity **normalized in software** (no
  `gpio_set_inover`) so the HAL stays portable (F2a).
- [x] 60 s auto-disarm as a driver-level safety default. In
  `drivers/hv_charger::hv_charger_tick` (F2b); configurable but
  never zero by accident.
- [x] `picoemp_pulse(width_us)` — CPU-timed pulse with interrupts
  disabled. In `drivers/emfi_pulse` (F2b). PIO-driven triggered
  fire deferred to F4 service layer.

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

- [x] Dropped `PIN_EXT1 = 27` (collided with the charge-on LED on
  real v2.x). Confirmed by the maintainer on the physical board
  2026-04-23. The legacy `glitcher.c` carried it as a stale
  upstream faultier relic; v3 does not port it.
- [x] Dropped `PIN_LED_STATUS = 25` (Pico-module relic; v2.x
  has no LED on GP25). Real STATUS LED is GP10 (F2a).
- [x] Dropped `PIN_MUX0/1/2 = GP1/GP2/GP3` (also faultier relics;
  those GPIOs are scanner CH1/CH2/CH3 on v2.x). No HW mux exists;
  the slot stays as a `#error` stub in `drivers/voltage_mux/`.
- [x] Collapsed `GlitchOutput_None / LP / HP / EMP` into two
  orthogonal axes: *which service owns the path* (EMFI on CDC0 vs
  crowbar on CDC1 — separate state machines, separate PIO SMs,
  separate IRQ flags) and *which physical output the crowbar PIO
  drives* (`crowbar_out_t` = LP/HP, picked at fire time). The EMP
  fourth value is gone — EMFI lives in its own service tree, not as
  an enum value of the same selector. F4 (EMFI) and F5 (crowbar)
  closed this together.

## Update policy for this document

The table above is the **checklist of ports**. Each phase commit
that consumes a row updates the Status cell in that same commit
(not in a separate doc commit). This keeps the doc honest without
introducing a review cadence out of step with the code.

New rows are added at the bottom when a legacy module surfaces that
wasn't anticipated. Nothing from legacy is deleted from this table —
rows marked `[x] discarded` or `[x] not ported` stay for audit
traceability.

# FaultyCat v3 firmware — architecture

## One-liner

Layered C/C++ firmware for RP2040, running on the existing FaultyCat
**v2.x hardware** (no board change). Exposes a USB composite device
that talks to a Python host tool (`faultycmd`) for two distinct
attack types — **EMFI** (electromagnetic, legacy FaultyCat speciality)
and **crowbar** (voltage glitching, added in HW v2.1) — plus a **JTAG /
SWD pinout scanner** and a standard **CMSIS-DAP** probe.

See [`FAULTYCAT_REFACTOR_PLAN.md`](../FAULTYCAT_REFACTOR_PLAN.md) for
the full phased roadmap (F0 → F11) and the 16 frozen design decisions.
This document describes the **layering and data flow**, not the plan.

## Status snapshot (as of v3.0-f10)

Branch `rewrite/v3`, last tag `v3.0-f10` (2026-04-29). F6 is
code-complete + spec-compliant but **not tagged** — physical gate
blocked by the TXS0108EPW level shifter on the scanner header (see
`HARDWARE_V2.md §2`). F7 deferred until that gate clears. F8 closed
2026-04-28 (JTAG / scanner / BusPirate / serprog from blueTag — see
`JTAG_INTERNALS.md`). F9 closed same day (campaign manager + SWD
bus mutex + binary host_proto — see `MUTEX_INTERNALS.md`). F10
closed 2026-04-29 — `host/faultycmd-py/`, the host tool. **Plan §1
#6 was overridden 2026-04-28**: original spec was a Rust workspace
+ ratatui TUI; the implementation is Python + Textual TUI + Rich
CLI based on team familiarity + reuse of the four legacy reference
clients. Wire protocols are unchanged.

| Phase | Tag | Status |
|-------|-----|--------|
| F0 — bootstrap + vendoring + docs + CI | `v3.0-f0` | ✓ closed |
| F1 — HAL + host tests | `v3.0-f1` | ✓ closed |
| F2a — drivers low-risk (LEDs, buttons, ADC, scanner, trigger) | `v3.0-f2a` | ✓ closed |
| F2b — drivers HV (crowbar, HV charger, EMFI pulse; 2 commits SIGNED) | `v3.0-f2b` | ✓ closed |
| F3 — USB composite descriptor (4×CDC + vendor + HID) + magic-baud BOOTSEL + diag on CDC2 | `v3.0-f3` | ✓ closed |
| F4 — glitch engine EMFI (service, PIO-driven triggered fire) | `v3.0-f4` | ✓ closed |
| F5 — glitch engine crowbar (service, PIO-driven triggered fire on pio0/SM1, IRQ 1) | `v3.0-f5` | ✓ closed |
| F6 — SWD core (debugprobe MIT port to swd_phy + scratch swd_dp / swd_mem; CDC2 shell) | — | code-complete + spec-compliant; **physical gate blocked** by TXS0108EPW HW path (see `HARDWARE_V2.md §2`). Open-drain PIO emulation in place; canonical raspberrypi/debugprobe also fails through the same HW path, confirming the bug is HW. Not tagged. |
| F7 — CMSIS-DAP v2 + v1 daplink_usb | — | deferred until F6 physical gate passes (HW bypass on the TXS0108E) |
| F8 — JTAG core + pinout scanner + BusPirate + serprog (blueTag) | `v3.0-f8` | ✓ closed — F8-1 `services/jtag_core/` (CPU bit-bang TAP + IDCODE chain). F8-2 `services/pinout_scanner/` (P(8,4) / P(8,2) brute-force scan + first-match). F8-3 unified CDC2 shell dispatcher. F8-4 `services/buspirate_compat/` (streaming BPv1 BBIO + OOCD JTAG sub-mode). F8-5 `services/flashrom_serprog/` (streaming serprog v1 + 4-pin CPU SPI bit-bang). F8-6 polish: 3-read consistency check on `pinout_scan_jtag`/`_swd` rejects bus-noise false positives empirically observed when a non-JTAG device is wired to the scanner header; `pump_shell_cdc` breaks out on mode-switch so the trailing `\n` of `\r\n` doesn't bleed into the new binary parser; new `docs/JTAG_INTERNALS.md`. Disconnect detection in main loop fires `bp_on_exit_cb` / `sp_on_exit_cb` if the host drops DTR mid-session. Diag snapshot gagged while in binary modes. Physical smoke 2026-04-28 on v2.2 board: 13/13 checks green (golden + regression). |
| F9 — Campaign manager + SWD mutex | `v3.0-f9` | ✓ closed — F9-1 `services/swd_bus_lock/` (volatile-flag cooperative mutex over the scanner-header SWD bus, 4 owner tags IDLE/CAMPAIGN/SCANNER/DAPLINK, single-owner no-reentrance; 13 host tests). F9-2 `services/campaign_manager/` (6-state machine over cartesian sweep + 256-entry × 28 B result ringbuffer + pluggable step executor with no-op default; 27 host tests). F9-3 engine adapters in `apps/faultycat_fw/main.c` — `campaign_executor_emfi/_crowbar` blocking-with-cooperative-yield; verify hook acquires/releases swd_bus_lock around a no-op call (F-future plugs real SWD post-fire verify). Shell `campaign <subcmd>` for status/stop/drain/`demo crowbar` smoke. F9-4 `services/host_proto/campaign_proto/` — CRC16-CCITT framing extending emfi_proto / crowbar_proto with CAMPAIGN_CONFIG/START/STOP/STATUS/DRAIN opcodes; engine implied by CDC; 17 host tests. F9-5 `tools/campaign_client.py` reference pyserial CLI mirroring emfi/crowbar_client.py. F9-6 polish: bumped CROWBAR_PROTO_MAX_PAYLOAD from 64 → 512 (DRAIN replies were silently dropped); made pump_emfi/crowbar_cdc reply[768] static (defensive vs stack overflow in deep executor wait loops). Smoke 2026-04-28: `campaign demo crowbar` shell + `campaign_client.py configure → start → watch` both stream complete sweeps end-to-end on v2.2. |
| F10 — faultycmd Python (Textual TUI + Rich CLI) | `v3.0-f10` | ✓ closed 2026-04-29 — `host/faultycmd-py/` package: F10-1 framing + USB enum, F10-2 protocols.{emfi,crowbar,campaign}, F10-3 protocols.scanner text-shell wrapper, F10-4 click+Rich CLI (`faultycmd info/emfi/crowbar/campaign/scanner` command groups), F10-5 Textual 4-panel dashboard (EMFI / Crowbar / Campaign / Diag CDC2 tail), F10-6 ruff lint + 3-version pytest matrix CI workflow (.github/workflows/host-py.yml) + `python -m faultycmd` entry point + PyInstaller smoke. 85 host-tests pass on Python 3.10/3.11/3.12. Real-device smoke verified end-to-end: `faultycmd info` enumerates the 4 CDCs, `faultycmd campaign --engine crowbar configure → start → watch` streams a 6-step LP sweep into a Rich Live table, the `faultycmd tui` dashboard launches with all 4 panels populated. Plan §1 #6 override (Rust → Python) documented in §F10 cierre + project memory. Legacy reference clients (`tools/{emfi,crowbar,campaign}_client.py` + `tools/{swd,jtag,scanner}_diag.py`) stay in tree as deprecated debug fallback until F11 archive. |
| F11 — Hardening, docs, release | — | pending |

Current tree health:
- **9 drivers** implemented (8 active + `voltage_mux` stub) under `drivers/`.
- **USB composite** up on VID:PID `1209:fa17`. 10 interfaces
  (4×CDC + Vendor + HID). 16/16 endpoints used (hard RP2040 limit).
  BOS + MS OS 2.0 descriptors for Windows WinUSB auto-bind.
  Magic baud 1200 on any CDC → `reset_usb_boot()` (remote BOOTSEL).
  CDC0 now owned by `host_proto/emfi_proto`.
- **HAL** headers live: `gpio`, `time` (+busy_wait +irq ctl), `adc`,
  `pwm`, `pio` ✓ F4-1, `dma` ✓ F4-2. `usb` stays `#error` stub
  (won't be lifted — TinyUSB is the abstraction).
- **`services/glitch_engine/emfi/`** complete: `emfi_pio`,
  `emfi_capture`, `emfi_campaign`. **`services/host_proto/emfi_proto/`**
  live on CDC0.
- **`services/glitch_engine/crowbar/`** complete: `crowbar_pio`,
  `crowbar_campaign`. **`services/host_proto/crowbar_proto/`** live
  on CDC1. `apps/faultycat_fw/main.c` runs `pump_crowbar_cdc()` and
  `crowbar_campaign_tick()` alongside the EMFI counterparts. The
  F2b CROWBAR demo cycle (LP→HP→NONE every 2 s) was removed in F5-4
  — the gate is operator-controlled via `crowbar_proto` now.
- **`services/swd_core/`** complete: `swd_phy` (debugprobe MIT
  port — 11-instruction PIO program with hand-encoded opcodes,
  runtime SWCLK/SWDIO/nRST pins, pio1/SM0), `swd_dp` (wire
  protocol from scratch — parity, ACK, READ/WRITE_DP, AP, abort,
  connect via line-reset + JTAG-to-SWD switch + DPIDR), `swd_mem`
  (ADIv5 MEM-AP single-AP read32/write32 via CSW + TAR + DRW +
  pipelined RDBUFF). `apps/faultycat_fw/main.c` adds a line-buffered
  text shell on CDC2 with 7 commands (`?`, `swd init/deinit/freq/
  connect/read32/write32/reset`) and lazy-init defaults to scanner
  header CH0/CH1/CH2. `tools/swd_diag.py` is the pyserial reference
  client.
- **`services/jtag_core/`** (F8-1) complete: `jtag_core.{c,h}` —
  CPU bit-bang TAP controller with `jtag_init`, `jtag_deinit`,
  `jtag_reset_to_run_test_idle`, `jtag_assert_trst`,
  `jtag_detect_chain_length`, `jtag_read_idcodes`,
  `jtag_idcode_is_valid`, `jtag_permutations_count`. Function
  shapes adapted from `third_party/blueTag/src/blueTag.c` under MIT
  (attribution at file head); reimplemented against `hal/gpio` so
  the v3 layered model holds. CDC2 shell extended with `jtag init/
  deinit/reset/trst/chain/idcode` (output prefix `JTAG:` so
  `tools/jtag_diag.py` can demux from F6 SWD replies). Soft-lock
  vs `swd_phy` enforced shell-side: `swd init` while JTAG is held
  returns `SWD: ERR jtag_in_use`, and vice versa. F9 promotes the
  soft-lock to a `mutex_t`.
- **`services/swd_bus_lock/`** (F9-1) complete:
  `swd_bus_lock.{c,h}` — service-layer mutual exclusion over the
  shared scanner-header SWD/JTAG bus. Volatile bool + owner tag,
  cooperative single-core (no IRQ-side acquires). API:
  `swd_bus_acquire(who, timeout_ms)`, `swd_bus_try_acquire(who)`,
  `swd_bus_release(who)`, `swd_bus_owner()`, `swd_bus_is_held()`.
  Owner enum: IDLE / CAMPAIGN / SCANNER / DAPLINK. Static priority
  is call-site responsibility (campaign > scanner > daplink_host
  per plan §4); the lock itself is FIFO-fair exclusion only.
  Coexists with F8-1's shell-level soft-lock — orthogonal layers
  for different consumer kinds.
- **`services/campaign_manager/`** (F9-2 + F9-3) complete:
  `campaign_manager.{c,h}` — 6-state machine
  (IDLE/CONFIGURING/SWEEPING/DONE/STOPPED/ERROR) over cartesian
  (delay, width, power) sweep. Pluggable step executor; default
  `campaign_noop_executor` lets host tests drive without engines.
  256-entry × 28 B result ringbuffer with overflow-drop counter.
  `apps/faultycat_fw/main.c` registers
  `campaign_dispatch_executor` which picks `campaign_executor_emfi`
  or `_crowbar` per `cfg->engine`. Engine adapters block with
  cooperative yield (usb_composite_task + emfi/crowbar pumps every
  ms) so a long step doesn't starve TinyUSB. Verify hook wraps
  `swd_bus_try_acquire(CAMPAIGN)` / `release` around a no-op call —
  F-future plugs in real `swd_dp_read32` against a target baseline.
  Shell on CDC2 gains `campaign status / stop / drain [<n>] / demo
  crowbar` for HV-safe physical smoke without HV cap.
- **`services/host_proto/campaign_proto/`** (F9-4) complete:
  shared payload helpers (`decode_config`, `apply_config`,
  `serialize_status`, `serialize_drain`) that emfi_proto (CDC0) and
  crowbar_proto (CDC1) call to handle CAMPAIGN_* opcodes 0x20..0x24.
  Engine implied by which CDC received the command — wire format
  identical. CONFIG payload 40 B (10×u32 LE), STATUS reply 20 B,
  DRAIN reply 1 B n + n × 28 B records (n capped at 18 to fit
  inside the 512 B EMFI_PROTO_MAX_PAYLOAD). F9-4 polish bumped
  CROWBAR_PROTO_MAX_PAYLOAD from 64 → 512 to match (its
  write_frame guard was silently rejecting drain replies > 64 B
  before the bump) and made pump_emfi/crowbar_cdc reply[768]
  static (defensive — F9-3's deep executor-wait-loop call stack
  could otherwise overflow the default 2 KB main-thread stack).
- **F9 reference client**: `tools/campaign_client.py` mirrors the
  emfi/crowbar_client.py pyserial pattern. Subcommands
  ping/configure/start/stop/status/drain/watch over the F9-4 wire
  protocol. Watch loop intentionally inserts a 30 ms gap between
  STATUS and DRAIN requests to dodge an executor-wait-loop dispatch
  ordering quirk that F-future async refactor will eliminate.
- **F8-6 polish** (2026-04-28) — closed F8 with two empirical fixes
  and the JTAG_INTERNALS.md reference doc:
  - `services/pinout_scanner/` gains a 3-read consistency check on
    every candidate match (`PINOUT_SCAN_CONFIRM_READS`). Fixed an
    observed false positive when an RP2040 was wired to the scanner
    header — bus noise forwarded through the TXS0108E produced
    `0x6B5AD5AD` which passed `jtag_idcode_is_valid` even though
    RP2040 has no JTAG TAP. Real silicon yields the same IDCODE on
    every read; pseudo-random noise rarely repeats.
  - `apps/faultycat_fw/main.c::pump_shell_cdc` breaks out of the
    per-batch loop the moment `process_shell_line` flips
    `s_shell_mode` away from TEXT. Without this, the trailing `\n`
    of the operator's `\r\n` would land in the new BusPirate /
    serprog parser as 0x0A and emit a spurious "BBIO1" reply.
  - `docs/JTAG_INTERNALS.md` documents F8's full wire stack — TAP
    state machine, BusPirate / serprog protocol surface, scanner
    false-positive analysis, mutual-exclusion contract, physical
    smoke results.
- **`services/flashrom_serprog/`** (F8-5) complete:
  `flashrom_serprog.{c,h}` — streaming Serial Flasher Protocol v1
  (the spec flashrom's `serprog` backend speaks). Same structural
  pattern as F8-4: 14-state state machine consuming bytes one at a
  time, callback-based SPI primitive (so tests stub it without
  hal_fake_pio). Upstream blueTag uses pico-sdk's `hardware_spi`
  peripheral on fixed pins GP0..GP3; we go with a 4-pin CPU
  bit-bang (mode 0, MSB-first) so the operator picks any 4 scanner
  channels for CS / MOSI / MISO / SCK. Yield hook fires every 128
  bytes of SPIOP traffic to keep `tud_task` and the glitch
  campaigns alive during a multi-MB chip read. `apps/
  faultycat_fw/main.c` adds bridge callbacks (`sp_write_byte_cb`,
  `sp_xfer_byte_cb`, `sp_cs_set_cb`, `sp_yield_cb`,
  `sp_on_exit_cb`), a `serprog enter [<cs> <mosi> <miso> <sck>]`
  command, and disconnect detection — when DTR drops on CDC2 mid
  session the appropriate `*_on_exit_cb` runs, restoring scanner
  pins to plain inputs so `scanner_io` / `swd_phy` / `jtag_core`
  can re-claim them. Programmer name reported via S_CMD_Q_PGMNAME
  is `"FaultyCat"` (16-byte NUL-padded).
- **`services/buspirate_compat/`** (F8-4) complete:
  `buspirate_compat.{c,h}` — streaming BusPirate v1 binary protocol
  (BBIO entry + OpenOCD JTAG sub-mode). 14-state state machine
  consumes bytes one at a time so OpenOCD's CMD_TAP_SHIFT
  (potentially thousands of bits) never blocks the main loop.
  Algorithm shape adapted from blueTag@v2.1.2's
  `src/modules/openocd/openocdJTAG.c` (MIT) but reworked from
  blocking `getc(stdin)` + 4 KB stage buffer to streaming
  feed_byte(). Callback-based JTAG clocker so the test build can
  mock both the byte sink and the JTAG primitive. `apps/
  faultycat_fw/main.c` adds bridge callbacks (`bp_write_byte_cb`,
  `bp_jtag_clock_bit_cb`, `bp_on_exit_cb`) and a shell-level
  `buspirate enter [<tdi> <tdo> <tms> <tck>]` command that pre-
  inits jtag_core with the requested pinout (defaults
  scanner CH0..CH3) and flips `s_shell_mode` to BUSPIRATE.
  `pump_shell_cdc` routes every byte through
  `buspirate_compat_feed_byte` while in that mode; 0x0F fires
  `bp_on_exit_cb` which deinits jtag_core and flips back to text.
  `diag_printf` and `print_snapshot` gag themselves while
  `s_shell_mode != SHELL_MODE_TEXT` to keep the binary stream
  clean for OpenOCD.
- **`services/pinout_scanner/`** (F8-2) complete:
  `pinout_scanner.{c,h}` — pure k-permutation iterator
  (`pinout_perm_init` / `_next` / `_total`) plus `pinout_scan_jtag`
  (P(8,4) = 1680 candidates, validated by `jtag_idcode_is_valid`)
  and `pinout_scan_swd` (P(8,2) = 56 candidates, validated by
  `swd_dp_connect` returning `SWD_ACK_OK` with non-zero DPIDR).
  Shell on CDC2 gains `scan jtag` and `scan swd [<targetsel_hex>]`
  (default TARGETSEL = `SWD_DP_TARGETSEL_RP2040_CORE0`). The
  candidate-iteration progress callback in `apps/faultycat_fw/
  main.c::scan_yield_progress` calls `usb_composite_task`,
  `pump_emfi_cdc`, `pump_crowbar_cdc`, `emfi_campaign_tick` and
  `crowbar_campaign_tick` between candidates and prints
  `SCAN: progress N/total` every 100 iterations — long scans don't
  starve TinyUSB or stall an active glitch campaign. Reference
  client `tools/scanner_diag.py` streams the progress + verdict.
- **404 unit tests** across 29 binaries, all green under
  `cmake --preset host-tests && ctest --preset host-tests`.
  F8-1 added `test_jtag_core` (24 cases) plus a generic
  `hal_fake_gpio` edge sampler + per-pin input-script API;
  F8-2 added `test_pinout_scanner` (13);
  F8-4 added `test_buspirate_compat` (22 — BBIO + OOCD subcmds +
  TAP_SHIFT bit packing + max-len clamp + OpenOCD-like session);
  F8-5 added `test_flashrom_serprog` (25 — query cmds + S_BUSTYPE
  accept/reject + SPIOP read-only/write-only/write+read JEDEC RDID
  + yield throttling + handshake session).
  F9-1 added `test_swd_bus_lock` (13 — owner tags / contention /
  no re-entrance / wrong-owner safety / all consumers acquire);
  F9-2 added `test_campaign_manager` (27 — axis math, total +
  step_to_params, state machine, ringbuffer, custom executor,
  reconfigure-mid-sweep rejection, 28 B record size);
  F9-4 added `test_campaign_proto` (17 — config decode + apply,
  status/drain serialize with multiple cap rules, constants
  self-check). End-to-end `pinout_scan_jtag/_swd` and the F9-3
  engine adapter pumps are deferred to physical smoke.
- **CI**: parallel `host-tests` + `fw-release` jobs on every push.
- **7 HV-SIGNED commits** in history: `f450d43` (hv_charger),
  `69792ac` (emfi_pulse), F4-3 (`emfi_pio` + driver PIO attach),
  F4-5 (`emfi_campaign` + 100 ms HV invariant), F4-6 (`emfi_proto`
  + main integration), F5-2 (`crowbar_pio` — PIO ownership of the
  MOSFET gate), F5-3 (`crowbar_campaign` — break-before-make
  hand-off). F6 added zero signed commits — pure PIO bit-bang on
  3.3 V logic, no HV path touched. See `docs/SAFETY.md`.

**PIO instance allocation (frozen at F4-1, extended at F5-2 / F6-2):**
`pio0` belongs to the glitch engines — SM 0 is used by
`services/glitch_engine/emfi/emfi_pio` for trigger+delay+pulse of
GP14 (raises IRQ 0); SM 1 is used by
`services/glitch_engine/crowbar/crowbar_pio` for trigger+delay+pulse
of GP16 or GP17 (chosen at fire time, raises IRQ 1). The two
engines coexist on the same PIO instance with disjoint state
machines AND disjoint IRQ flags so neither can spuriously mark the
other done. `pio1` SM 0 is now used by
`services/swd_core/swd_phy` (debugprobe-derived, runtime
SWCLK/SWDIO pins). F8-1 deliberately keeps `services/jtag_core` on
**CPU bit-bang via `hal/gpio`**, NOT PIO — the TXS0108EPW level
shifter on the scanner header caps wire rate at ~25 MHz anyway and
matching blueTag's proven pure-CPU path keeps F8-1 testable host-
side without a PIO simulator. `pio1` SM 1..3 stay reserved for
`target-uart` (F8) and the eventual buspirate-compat SPI bit-banger
(F8-4 if we want hardware-rate flashrom), splitting across those
consumers.

**HAL extension (F6-2):** `hal/include/hal/pio.h` gained
`out_pin_base/count`, `wrap_target/end` (relative to program start),
`out_shift_right`, `in_shift_right` in `hal_pio_sm_cfg_t`, plus
`hal_pio_sm_exec()` (single-instruction inject for SM bootstrap)
and `hal_pio_sm_set_clkdiv_int()` (runtime SWCLK retune). Backwards-
compatible — existing EMFI / crowbar configures are unaffected.

## Layers

`✓ = landed` · `→ = current phase` · `… = scheduled`

```
┌────────────────────────────────────────────────────────────────────────────┐
│  apps/faultycat_fw                    ←  single main, composes services      │
│    main.c (diag loop on CDC2 scanner + HV/EMFI/crowbar)           ✓ F3       │
├────────────────────────────────────────────────────────────────────────────┤
│  services/                            ←  attack logic, protocol handlers     │
│    glitch_engine/emfi/                │    EMFI campaign + PIO fire  ✓ F4    │
│    glitch_engine/crowbar/             │    crowbar campaign + PIO    ✓ F5    │
│    swd_core/                          │    debugprobe-derived (phy)  ✓ F6    │
│      + swd_dp + swd_mem (scratch)     │    + DPIDR/MEM-AP shell      ✓ F6    │
│    daplink_usb/                       │    CMSIS-DAP v2 + v1 HID     … F7    │
│    jtag_core/                         │    blueTag-derived (CPU)     ✓ F8-1  │
│    pinout_scanner/                    │    JTAGulator over scanner   ✓ F8-2  │
│    buspirate_compat/                  │    blueTag BBIO + OpenOCD    ✓ F8-4  │
│    flashrom_serprog/                  │    serprog v1 + SPI bitbang  ✓ F8-5  │
│    host_proto/emfi_proto              │    binary framing on CDC0    ✓ F4    │
│    host_proto/crowbar_proto           │    binary framing on CDC1    ✓ F5    │
│    host_proto/campaign_proto          │    streamed sweep results    … F9    │
├────────────────────────────────────────────────────────────────────────────┤
│  usb/                                 ←  composite descriptor                │
│    usb_composite.c + usb_descriptors.c + dap_stub.c              ✓ F3        │
│    4×CDC (emfi/crowbar/scanner/target-uart) + vendor + HID  16/16 eps        │
│    BOS + MS OS 2.0 (WinUSB auto-bind)   magic-baud 1200 → BOOTSEL            │
├────────────────────────────────────────────────────────────────────────────┤
│  drivers/                             ←  knows FaultyCat v2.x pinout         │
│    board_v2.h     (single-source pinout)                        ✓ F2a        │
│    ui_leds        (GP9/GP10/GP27 + 500 ms HV hysteresis)        ✓ F2a        │
│    ui_buttons     (ARM GP28, PULSE GP11 — SW polarity norm)     ✓ F2a        │
│    target_monitor (ADC ch3 / GP29, direct, no divider)          ✓ F2a        │
│    scanner_io     (GP0..GP7, 8 channels)                        ✓ F2a        │
│    ext_trigger    (GP8 + pull config)                           ✓ F2a        │
│    crowbar_mosfet (GP16 LP / GP17 HP + break-before-make)       ✓ F2b        │
│    voltage_mux    (stub — no HW mux on v2.x)                    ✓ F2b        │
│    hv_charger     (GP20 PWM flyback + GP18 CHARGED, 60s auto)   ✓ F2b SIGNED │
│    emfi_pulse     (GP14 HV pulse, CPU-timed manual)             ✓ F2b SIGNED │
├────────────────────────────────────────────────────────────────────────────┤
│  hal/                                 ←  thin wrapper over pico-sdk          │
│    gpio   ✓ F1       time  ✓ F1 (+busy/irq in F2b)                          │
│    adc    ✓ F2a      pwm   ✓ F2b                                            │
│    pio    ✓ F4-1                 dma   ✓ F4-2                               │
│    usb    #error stub (will NOT be lifted — TinyUSB IS our abstraction)     │
├────────────────────────────────────────────────────────────────────────────┤
│  third_party/pico-sdk @ 2.1.1         ←  BSD-3, pinned                       │
│  third_party/Unity    @ v2.6.1        ←  MIT, pinned — host tests            │
│  third_party/debugprobe @ v2.3.0      ←  MIT, pinned — F7                    │
│  third_party/blueTag  @ v2.1.2        ←  MIT, pinned — F8                    │
│  third_party/free-dap (master HEAD)   ←  BSD-3, ref only (not compiled)      │
│  third_party/faultier (1c78f3e)       ←  NO LICENSE, ref only (not compiled) │
│  third_party/cmsis-dap (headers copy) ←  Apache-2.0 — F7                     │
└────────────────────────────────────────────────────────────────────────────┘
```

Rules:
1. Upper layers may only call APIs from directly-lower layers.
2. `hal/` contains **no FaultyCat-specific knowledge** — no pin numbers,
   no board config, nothing.
3. `drivers/` contain **no policy** — they drive the hardware and return
   status, they don't decide what to do with it.
4. `services/` orchestrate multiple drivers to implement features.
5. `usb/` is crosscutting — multiple services read/write through the
   composite, but the descriptor is defined once.

## USB composite (F3)

```
Configuration Descriptor (Miscellaneous class, IAD-based)
├── IAD + CDC 0 "EMFI Control"       IF 0 (notif) + IF 1 (data)  ✓ F4 emfi_proto
├── IAD + CDC 1 "Crowbar Control"    IF 2 (notif) + IF 3 (data)  ✓ F5 crowbar_proto
├── IAD + CDC 2 "Scanner Shell"      IF 4 (notif) + IF 5 (data)  ✓ F6 swd_shell + diag (F8 → full blueTag shell)
├── IAD + CDC 3 "Target UART"        IF 6 (notif) + IF 7 (data)  → PIO UART passthru
├── Vendor IF   "CMSIS-DAP v2"       IF 8 (2 bulk eps)           → daplink_usb (v2)
└── HID IF      "CMSIS-DAP v1"       IF 9 (1 int ep)             → daplink_usb (v1)
```

**Endpoints:** 4×CDC = 12, vendor = 2, HID = 1, control EP0 = 1 → **16/16**.
The RP2040 has no headroom; any new USB interface must sacrifice an
existing one. Contingency paths (§4 of the plan): drop HID, fold
target-UART into scanner CDC.

**VID:PID:** `1209:FA17` in development (pid.codes dev range). A proper
pid.codes allocation happens at v3.0.0 release (F11).

## SWD bus arbitration (F9)

Three consumers can want SWD concurrently:

- `daplink_usb` — external host (OpenOCD / probe-rs / pyOCD).
- `glitch_engine/*` — post-glitch verification reads.
- `pinout_scanner` — during scan.

**Policy:** static priority `campaign > scanner > daplink_host`, with a
pico-sdk `mutex_t` + explicit timeout. If `daplink_usb` requests the bus
while held, it responds `DAP_ERROR (busy)` so the host retries. Full
state machine in this doc, updated in F9.

## Host tool (F10) — closed `v3.0-f10` (2026-04-29)

`host/faultycmd-py/` is the Python host tool. Plan §1 decision #6
originally specified a Rust workspace + ratatui TUI; the 2026-04-28
override (see plan §F10 cierre block + memory
`project_faultycmd_python_override.md`) switched to Python +
Textual + Rich based on team familiarity, faster iteration, direct
reuse of the F4/F5/F9 reference clients, and a lower contributor
onboarding cost. The wire protocols (host_proto/* opcodes, frame
format, mutex contract) are unchanged — only the host language
changed.

```
faultycmd.framing               — CRC16-CCITT helper + frame builder
faultycmd.usb                   — port → CDC mapping (udevadm helper)
faultycmd.protocols.emfi        — F4 emfi_proto client (CDC0)
faultycmd.protocols.crowbar     — F5 crowbar_proto client (CDC1)
faultycmd.protocols.campaign    — F9-4 campaign_proto over CDC0/CDC1
faultycmd.protocols.scanner     — text-shell wrapper over CDC2
                                  (consolidates F6 swd / F8-1 jtag /
                                   F8-2 scan / F8-4 buspirate / F8-5
                                   serprog mode-switch helpers)
faultycmd.protocols.dap         — pyocd / cmsis-dap thin wrapper
                                  (stub until F7 daplink_usb lands)
faultycmd.cli                   — click-based CLI; Rich-rendered
                                  output (tables, progress bars,
                                  status panels)
faultycmd.tui                   — Textual app (HV / trigger / SWD /
                                  campaign panels + E/C/S/D hotkeys)
```

A campaign manager streams (delay, width, power) sweeps with SWD
verification after each glitch and captures the result on the host.

The four legacy reference clients (`tools/{emfi,crowbar,campaign}_
client.py`, `tools/{swd,jtag,scanner}_diag.py`) stay in the tree as
a debugging fallback through v3.0.0 release; F11 will archive them.

## What each phase delivers

See [`FAULTYCAT_REFACTOR_PLAN.md §6`](../FAULTYCAT_REFACTOR_PLAN.md#6-plan-por-fases-superpowers).

## Update policy for this document

This doc carries the **current-truth status snapshot** at the top of
the page and a stable architecture description below it. The
maintenance routine (set at v3.0-f2b):

1. On the commit that closes a phase (right before the tag), update
   the Status snapshot table with the new tag + date, and tick
   the relevant driver/service rows in the big tree diagram.
2. If the phase added a new entry (a new service, a new HAL API, a
   new third_party dep), add it to the diagram.
3. The architectural description (layers, rules, USB, SWD
   arbitration, host tool) does NOT rewrite; it only gets annotated
   when a frozen decision changes (which the plan §1 says won't
   happen).

This keeps reviewers who land on the repo cold able to read
`ARCHITECTURE.md` alone and know exactly what's built vs. what's
still on the roadmap.

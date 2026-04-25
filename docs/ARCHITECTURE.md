# FaultyCat v3 firmware — architecture

## One-liner

Layered C/C++ firmware for RP2040, running on the existing FaultyCat
**v2.x hardware** (no board change). Exposes a USB composite device
that talks to a Rust host tool (`faultycmd-rs`) for two distinct
attack types — **EMFI** (electromagnetic, legacy FaultyCat speciality)
and **crowbar** (voltage glitching, added in HW v2.1) — plus a **JTAG /
SWD pinout scanner** and a standard **CMSIS-DAP** probe.

See [`FAULTYCAT_REFACTOR_PLAN.md`](../FAULTYCAT_REFACTOR_PLAN.md) for
the full phased roadmap (F0 → F11) and the 16 frozen design decisions.
This document describes the **layering and data flow**, not the plan.

## Status snapshot (as of v3.0-f6)

Branch `rewrite/v3`, last tag `v3.0-f6` (2026-04-24).

| Phase | Tag | Status |
|-------|-----|--------|
| F0 — bootstrap + vendoring + docs + CI | `v3.0-f0` | ✓ closed |
| F1 — HAL + host tests | `v3.0-f1` | ✓ closed |
| F2a — drivers low-risk (LEDs, buttons, ADC, scanner, trigger) | `v3.0-f2a` | ✓ closed |
| F2b — drivers HV (crowbar, HV charger, EMFI pulse; 2 commits SIGNED) | `v3.0-f2b` | ✓ closed |
| F3 — USB composite descriptor (4×CDC + vendor + HID) + magic-baud BOOTSEL + diag on CDC2 | `v3.0-f3` | ✓ closed |
| F4 — glitch engine EMFI (service, PIO-driven triggered fire) | `v3.0-f4` | ✓ closed |
| F5 — glitch engine crowbar (service, PIO-driven triggered fire on pio0/SM1, IRQ 1) | `v3.0-f5` | ✓ closed |
| F6 — SWD core (debugprobe MIT port to swd_phy + scratch swd_dp / swd_mem; CDC2 shell) | `v3.0-f6` | ✓ closed |
| F7 — CMSIS-DAP v2 + v1 daplink_usb | — | **next** |
| F7 — CMSIS-DAP v2 + v1 daplink_usb | — | pending |
| F8 — JTAG core + pinout scanner + BusPirate + serprog | — | pending |
| F9 — Campaign manager + SWD mutex | — | pending |
| F10 — faultycmd-rs Rust workspace | — | pending |
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
- **263 unit tests** across 22 binaries, all green under
  `cmake --preset host-tests && ctest --preset host-tests`.
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
SWCLK/SWDIO pins; the rest of pio1 stays reserved for `target-uart`
(F8) and `jtag_core` + scanner bit-banging (F8), splitting the 3
remaining SMs across those consumers).

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
│    jtag_core/ pinout_scanner/         │    blueTag-derived           … F8    │
│    buspirate_compat/ flashrom_serprog/│    blueTag-derived           … F8    │
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

## Host tool (F10)

`host/faultycmd-rs/` is a Rust workspace:

```
faultycmd-core    — shared types, USB enumeration, logger
faultycmd-emfi    — client over CDC0
faultycmd-crowbar — client over CDC1
faultycmd-scanner — client over CDC2
faultycmd-dap     — thin wrapper around probe-rs talking to Vendor IF
faultycmd-cli     — clap-based CLI
faultycmd-tui     — ratatui-based dashboard (HV / trigger / SWD / campaign)
```

A campaign manager streams (delay, width, power) sweeps with SWD
verification after each glitch and captures the result on the host.

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

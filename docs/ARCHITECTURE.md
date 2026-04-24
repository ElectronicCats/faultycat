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

## Status snapshot (as of v3.0-f2b)

Branch `rewrite/v3`, last tag `v3.0-f2b` (2026-04-23).

| Phase | Tag | Status |
|-------|-----|--------|
| F0 — bootstrap + vendoring + docs + CI | `v3.0-f0` | ✓ closed |
| F1 — HAL + host tests | `v3.0-f1` | ✓ closed |
| F2a — drivers low-risk (LEDs, buttons, ADC, scanner, trigger) | `v3.0-f2a` | ✓ closed |
| F2b — drivers HV (crowbar, HV charger, EMFI pulse; 2 commits SIGNED) | `v3.0-f2b` | ✓ closed |
| F3 — USB composite descriptor (4×CDC + vendor + HID) | — | **next** |
| F4 — glitch engine EMFI (service, PIO-driven triggered fire) | — | pending |
| F5 — glitch engine crowbar (service) | — | pending |
| F6 — SWD core (debugprobe port) | — | pending |
| F7 — CMSIS-DAP v2 + v1 daplink_usb | — | pending |
| F8 — JTAG core + pinout scanner + BusPirate + serprog | — | pending |
| F9 — Campaign manager + SWD mutex | — | pending |
| F10 — faultycmd-rs Rust workspace | — | pending |
| F11 — Hardening, docs, release | — | pending |

Current tree health:
- **9 drivers** implemented (8 active + `voltage_mux` stub) under `drivers/`.
- **HAL** headers live: `gpio`, `time` (+busy_wait +irq ctl), `adc`,
  `pwm`. Still `#error` stubs: `pio` (F4), `dma` (F4), `usb` (won't be
  lifted — TinyUSB is the abstraction, see F3 note below).
- **75 unit tests** across 10 binaries, all green under
  `cmake --preset host-tests && ctest --preset host-tests`.
- **CI**: parallel `host-tests` + `fw-release` jobs on every push.
- **2 HV-SIGNED commits** in history: `f450d43` (hv_charger) and
  `69792ac` (emfi_pulse). See `docs/SAFETY.md`.

## Layers

`✓ = landed` · `→ = current phase` · `… = scheduled`

```
┌────────────────────────────────────────────────────────────────────────────┐
│  apps/faultycat_fw                    ←  single main, composes services      │
│    main.c (diag loop — provisional; F3 moves it to scanner CDC)   ✓ F2b      │
├────────────────────────────────────────────────────────────────────────────┤
│  services/                            ←  attack logic, protocol handlers     │
│    glitch_engine/emfi/                │    EMFI campaign + PIO fire  … F4    │
│    glitch_engine/crowbar/             │    voltage glitching         … F5    │
│    swd_core/                          │    debugprobe-derived        … F6    │
│    daplink_usb/                       │    CMSIS-DAP v2 + v1 HID     … F7    │
│    jtag_core/ pinout_scanner/         │    blueTag-derived           … F8    │
│    buspirate_compat/ flashrom_serprog/│    blueTag-derived           … F8    │
│    host_proto/ {emfi,crowbar,campaign}│    binary CDC protocols      … F4/5/9│
├────────────────────────────────────────────────────────────────────────────┤
│  usb/                                 ←  composite descriptor                │
│    10 interfaces (4×CDC + Vendor + HID)   16/16 endpoints       → F3         │
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
│    pio    #error stub → F4       dma   #error stub → F4                     │
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
├── IAD + CDC 0 "EMFI Control"       IF 0 (notif) + IF 1 (data)  → glitch_engine/emfi
├── IAD + CDC 1 "Crowbar Control"    IF 2 (notif) + IF 3 (data)  → glitch_engine/crowbar
├── IAD + CDC 2 "Scanner Shell"      IF 4 (notif) + IF 5 (data)  → pinout_scanner (shell)
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

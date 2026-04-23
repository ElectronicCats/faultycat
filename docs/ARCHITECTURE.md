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

## Layers

```
┌────────────────────────────────────────────────────────────────────────┐
│  apps/faultycat_fw                    ←  single main, composes services  │
├────────────────────────────────────────────────────────────────────────┤
│  services/                            ←  attack logic, protocol handlers │
│    glitch_engine/emfi/                │    F4                           │
│    glitch_engine/crowbar/             │    F5 (faultier-inspired, clean) │
│    swd_core/                          │    F6 (debugprobe-derived, MIT)  │
│    jtag_core/ pinout_scanner/         │    F8 (blueTag-derived, MIT)     │
│    buspirate_compat/ flashrom_serprog/│    F8                           │
│    daplink_usb/                       │    F7 (CMSIS-DAP v2 + v1 HID)    │
│    host_proto/ {emfi,crowbar,campaign}│    F4/F5/F9 binary CDC protocols │
├────────────────────────────────────────────────────────────────────────┤
│  usb/                                 ←  composite descriptor            │
│    10 interfaces (4×CDC + Vendor + HID)    F3, 16/16 endpoints used     │
├────────────────────────────────────────────────────────────────────────┤
│  drivers/                             ←  knows FaultyCat v2.x pinout     │
│    ui_leds ui_buttons hv_charger      │    F2                           │
│    emfi_pulse crowbar_mosfet          │                                 │
│    ext_trigger target_monitor         │                                 │
│    voltage_mux scanner_io             │                                 │
├────────────────────────────────────────────────────────────────────────┤
│  hal/                                 ←  thin wrapper over pico-sdk      │
│    gpio pio dma time usb adc pwm      │    F1, with native-side fakes    │
├────────────────────────────────────────────────────────────────────────┤
│  third_party/pico-sdk                 ←  HAL for RP2040 (BSD-3, pinned)  │
└────────────────────────────────────────────────────────────────────────┘
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
This doc is kept **stable** across phases; each phase's work either
fills in a block above or annotates the USB/arbitration tables.

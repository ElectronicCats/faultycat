# FaultyCat v3 — glitching techniques and modes

Fault injection on FaultyCat lives on **two independent axes** that
are often conflated. This document separates them and shows which
host commands and firmware services correspond to each combination.

| Axis | Question it answers | Values in v3.0 |
|---|---|---|
| **Technique** | *How* is the fault physically induced? | **EMFI** · **Crowbar** |
| **Mode** | *How many* shots and with what parameter strategy? | **Direct** (single shot) · **Campaign** (sweep) |

You can run either technique in either mode — they compose freely.
The TUI and CLI expose the matrix as four independent surfaces
(`e`, `b`, `p` with `--engine emfi|crowbar`).

---

## 1 · Techniques (the *how*)

Both techniques aim at the same target outcome — make a chip
misbehave for one instruction so a checked branch / authentication /
lifecycle gate fails open — but they reach it through different
physics, different hardware paths, and different parameter sets.

### 1.1 EMFI — electromagnetic fault injection

- **Mechanism.** Charge an HV reservoir cap (~250 V), then dump it
  into a small coil placed on the chip die. The transient B-field
  couples into the die's metal layers and flips bits while the cap
  discharges.
- **Hardware path.** `hv_charger` (GP20 PWM flyback → GP18
  `CHARGED`) → 250 V cap → `emfi_pulse` driver (GP14 HV pulse) →
  external EMFI coil tip on SMA.
- **Service.** `services/glitch_engine/emfi/` (`emfi_pio`,
  `emfi_capture`, `emfi_campaign` — the last name is historical;
  see §4).
- **Pulse-width units.** **µs** (microseconds).
- **Safety.** The 100 ms HV-charge invariant in
  [`docs/SAFETY.md`](SAFETY.md) §3 #5 applies here.

Single-shot configuration matrix (`emfi_config_t`):

| Field | Meaning |
|---|---|
| `trigger` | `IMMEDIATE` / `EXT_RISING` / `EXT_FALLING` / `EXT_PULSE_POS` / `EXT_PULSE_NEG` |
| `delay_us` | Trigger → fire latency, µs |
| `width_us` | Pulse duration on the coil, µs |
| `power` | HV charge target (ADC ticks on `target_monitor` ch3 / GP29) |

### 1.2 Crowbar — voltage glitching

- **Mechanism.** A power N-MOSFET briefly shorts the target's VCC
  to GND. The voltage dip violates setup/hold on the target's
  flip-flops, flipping bits as they latch.
- **Hardware path.** `drivers/crowbar_mosfet` — GP17 drives the LP
  path, GP16 drives the HP path (real glitch). Idle state is
  `CROWBAR_OUT_NONE` with break-before-make on every transition;
  during the fire window `crowbar_pio` owns the gate.
- **Service.** `services/glitch_engine/crowbar/` (`crowbar_pio`,
  `crowbar_campaign` — same historical-name caveat as EMFI; see §4).
- **Pulse-width units.** **ns** (nanoseconds — crowbar is ~3 orders
  of magnitude faster than EMFI).
- **Safety.** No HV cap is involved. The
  [`docs/SAFETY.md`](SAFETY.md) HV invariant does **not** apply to
  crowbar.

Single-shot configuration matrix (`crowbar_config_t`):

| Field | Meaning |
|---|---|
| `trigger` | Same five options as EMFI |
| `output` | `CROWBAR_OUT_NONE` / `_LP` (GP17) / `_HP` (GP16) — which MOSFET path the PIO drives |
| `delay_us` | Trigger → fire latency, µs |
| `width_ns` | Pulse duration on the MOSFET gate, ns |

### 1.3 Side by side

| | EMFI | Crowbar |
|---|---|---|
| Physical effect | Magnetic coupling into the die | Brief VCC↔GND short via MOSFET |
| Pulse-width unit | µs | ns (≈ 1000× faster) |
| "Power" axis means | HV cap charge target (ADC ticks) | Output path (`LP` / `HP`) |
| HV cap involved | yes (~250 V) | no |
| Driver pin | GP14 | GP17 (LP) / GP16 (HP) |
| PIO instance / SM / IRQ | `pio0` SM 0 / IRQ 0 | `pio0` SM 1 / IRQ 1 |
| USB CDC | CDC0 *"EMFI Control"* | CDC1 *"Crowbar Control"* |
| Wire protocol | `services/host_proto/emfi_proto/` | `services/host_proto/crowbar_proto/` |
| Host module | `faultycmd.protocols.emfi` | `faultycmd.protocols.crowbar` |
| Host CLI group | `faultycmd emfi ...` | `faultycmd crowbar ...` |
| TUI hotkey | `e` | `b` |
| HV safety invariant | applies (100 ms charge limit) | does **not** apply |

---

## 2 · Modes (the *how often*)

This is the axis the user-facing word "campaign" actually refers to
in v3.0. **The mode and the technique are independent** — campaign
mode drives either EMFI or Crowbar, and direct mode is available
for either as well.

### 2.1 Direct mode — one shot at a time

- **Wire pattern.** `PING → CONFIGURE → ARM → FIRE → STATUS →
  DISARM`. One config, one trigger, one pulse. The operator picks
  each value explicitly; the firmware never iterates parameters.
- **State machine.** Lives inside the per-engine service —
  `emfi_campaign.{c,h}` for EMFI and `crowbar_campaign.{c,h}` for
  crowbar. The `_campaign` suffix is **historical** — these files
  predate the sweep manager and orchestrate one fire end-to-end;
  see §4.
- **Wire opcodes** (per CDC):
  `0x01 PING · 0x10 CONFIGURE · 0x11 ARM · 0x12 FIRE · 0x13 DISARM · 0x14 STATUS`.
- **Host CLI.** `faultycmd emfi {ping,configure,arm,fire,disarm,status}`
  and the matching `faultycmd crowbar ...`.
- **TUI.** Hotkey `e` → `EmfiControlModal`. Hotkey `b` →
  `CrowbarControlModal`.

### 2.2 Campaign mode — sweep over a parameter grid

- **Wire pattern.** `CAMPAIGN_CONFIG → CAMPAIGN_START`, then the
  host polls `CAMPAIGN_STATUS` and drains results with
  `CAMPAIGN_DRAIN`; `CAMPAIGN_STOP` aborts mid-sweep.
- **What it sweeps.** A **cartesian product** of three axes —
  `delay`, `width`, and `power` — each expressed as a
  `(start, end, step)` triple. `step == 0` collapses an axis to a
  single value (just `start`). The total step count is
  `n_delay × n_width × n_power`.
- **Iteration order.** `power` innermost → `width` middle →
  `delay` outermost. The fastest-fluctuating axis sweeps first so
  the operator gets early visibility into how the target reacts
  along it.
- **Result capture.** Every step produces a 28-byte
  `campaign_result_t` (`step_n`, `delay`, `width`, `power`,
  `fire_status`, `verify_status`, `target_state`, `ts_us`),
  appended to a 256-entry ringbuffer in the firmware. Overflow
  drops the oldest entries and bumps a counter exposed in
  `CAMPAIGN_STATUS`.
- **Engine selection.** Explicit in `campaign_config_t.engine`
  (`CAMPAIGN_ENGINE_EMFI` or `CAMPAIGN_ENGINE_CROWBAR`). The
  firmware adapter `campaign_dispatch_executor` in
  `apps/faultycat_fw/main.c` routes each step to
  `campaign_executor_emfi` or `_crowbar`, which bridges the
  per-step API onto the engine's existing single-shot state
  machine.
- **CDC routing.** Campaign opcodes (`0x20..0x24`) ride on the
  **same** CDC as the engine they target — EMFI campaigns over
  CDC0, crowbar campaigns over CDC1. The engine is implied by the
  CDC plus the explicit `engine` byte in the config payload.
- **TUI MVP.** The campaign modal is currently locked to
  `engine=crowbar` (F11-0c). The CLI accepts either engine.
- **Verify hook.** After every fire the executor calls a verify
  callback that acquires `swd_bus_lock(CAMPAIGN)`, runs a no-op,
  and releases. A future phase plugs a real SWD `read32` against a
  target baseline into that hook. See
  [`docs/MUTEX_INTERNALS.md`](MUTEX_INTERNALS.md).
- **Host CLI.** `faultycmd campaign {configure,start,stop,status,
  drain,watch}` with `--engine emfi|crowbar`.
- **TUI.** Hotkey `p` → `CampaignControlModal`.

`campaign_config_t` shape:

| Field | Meaning |
|---|---|
| `engine` | `CAMPAIGN_ENGINE_EMFI` or `_CROWBAR` — picks the per-step executor |
| `delay` | `(start, end, step)` µs — same units in both engines |
| `width` | `(start, end, step)` — **µs** for EMFI, **ns** for crowbar |
| `power` | `(start, end, step)` — HV charge target (EMFI) / output path code (crowbar) |
| `settle_ms` | Wait between fires; `0` = no wait |

### 2.3 Side by side

| | Direct | Campaign |
|---|---|---|
| Shots per session | 1 | `n_delay × n_width × n_power` |
| Config knobs | 4 scalar fields | 3 axes × `(start, end, step)` + `engine` + `settle_ms` |
| Wire opcodes | `0x01..0x14` | `0x20..0x24` (multiplexed on the same CDC) |
| Engine bound by | which CDC the host opened | explicit `engine` byte in the config payload |
| Result data | `STATUS` reply of the last fire | 28 B record per step, streamed via `DRAIN` |
| Typical use | Bench tuning, single shot under a scope | Parameter-space search for an actual attack |
| Host CLI | `faultycmd {emfi,crowbar} {arm,fire,...}` | `faultycmd campaign {configure,start,watch,...}` |
| TUI hotkey | `e` (EMFI) / `b` (crowbar) | `p` |
| Firmware code | `services/glitch_engine/{emfi,crowbar}/*_campaign.{c,h}` | `services/campaign_manager/` + adapters in `apps/faultycat_fw/main.c` |
| Wire spec | `services/host_proto/{emfi,crowbar}_proto/` | `services/host_proto/campaign_proto/` |

---

## 3 · The 2 × 2

Putting both axes together, every fault-injection operation in v3.0
falls into exactly one of these four cells:

| | Direct mode (1 shot) | Campaign mode (sweep) |
|---|---|---|
| **EMFI** | `faultycmd emfi fire` · TUI `e` | `faultycmd campaign configure --engine emfi` · TUI `p` (CLI only in v3.0) |
| **Crowbar** | `faultycmd crowbar fire` · TUI `b` | `faultycmd campaign configure --engine crowbar` · TUI `p` |

The wire protocol, the per-engine state machine, and the sweep
manager are all **already shipped** for the full matrix; only the
TUI campaign-modal engine selector is locked to crowbar in this
release (F11-0c MVP — see the host README's "Status" section).

---

## 4 · Vocabulary caveat — "campaign" is overloaded

Inside the codebase the word *campaign* has historically meant two
different things, which is what makes this topic confusing:

1. **The per-engine state machine.** The files
   `services/glitch_engine/{emfi,crowbar}/*_campaign.{c,h}`
   pre-date the F9 sweep harness. In them "campaign" just meant
   "the orchestrator that walks one fire through configure → arm →
   fire → status → disarm". **They drive a single shot.** They do
   **not** sweep.
2. **The sweep manager.** `services/campaign_manager/`, added in
   F9, is the actual cartesian-product sweep harness. It owns its
   own state machine
   (`IDLE → CONFIGURING → SWEEPING → DONE/STOPPED/ERROR`) and
   plugs into one of the per-engine state machines above as its
   step executor.

To stay unambiguous this document — and the host CLI / TUI surfaces
— use **engine state machine** for sense (1) and **campaign** for
sense (2). A future cleanup may rename the engine state-machine
files to `*_engine.{c,h}` to align the code with the documentation;
the wire protocols stay unchanged either way.

---

## 5 · Where the code lives (jump map)

| Concept | Where |
|---|---|
| EMFI engine state machine | `services/glitch_engine/emfi/emfi_campaign.{c,h}` |
| EMFI PIO program | `services/glitch_engine/emfi/emfi_pio.{c,h}` |
| EMFI wire protocol | `services/host_proto/emfi_proto/` |
| EMFI host CLI / module | `faultycmd emfi ...` · `host/faultycmd-py/src/faultycmd/protocols/emfi.py` |
| Crowbar engine state machine | `services/glitch_engine/crowbar/crowbar_campaign.{c,h}` |
| Crowbar PIO program | `services/glitch_engine/crowbar/crowbar_pio.{c,h}` |
| Crowbar wire protocol | `services/host_proto/crowbar_proto/` |
| Crowbar host CLI / module | `faultycmd crowbar ...` · `host/faultycmd-py/src/faultycmd/protocols/crowbar.py` |
| Campaign sweep manager | `services/campaign_manager/` |
| Campaign wire protocol | `services/host_proto/campaign_proto/` |
| Campaign engine adapters | `apps/faultycat_fw/main.c::campaign_executor_emfi` / `_crowbar` |
| Campaign host CLI / module | `faultycmd campaign ...` · `host/faultycmd-py/src/faultycmd/protocols/campaign.py` |

## 6 · Related docs

- [`docs/SAFETY.md`](SAFETY.md) — HV / EMFI safety contract
  (100 ms charge invariant). Crowbar is HV-free and is not covered
  by this contract.
- [`docs/MUTEX_INTERNALS.md`](MUTEX_INTERNALS.md) — SWD bus mutex
  used by the campaign verify hook.
- [`docs/HARDWARE_V2.md`](HARDWARE_V2.md) — GPIO map for both
  hardware paths.
- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) §USB composite — where
  each CDC sits in the descriptor.
- [`host/faultycmd-py/README.md`](../host/faultycmd-py/README.md)
  — host install / usage, hotkeys, trigger polarity, trigger
  timeout.

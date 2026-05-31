# Target Serial Passthrough — Design (Increment 1)

**Date:** 2026-05-31
**Status:** approved (brainstorm), pending implementation plan
**Author:** Sabas + Claude

## Context

FaultyCat v3 exposes four USB-CDC interfaces. CDC3 (`USB_CDC_TARGET = 3`)
is reserved as "Target UART" but **never implemented** — today it only
echoes bytes back (`usb/src/usb_composite.c:85`, the `for (i = 3 ...)`
loop). The "F8 → PIO UART passthru" label scattered in the code is a
**stale, deferred tag**: the JTAG-era F8 phase closed without building any
UART. No hardware UART (uart0/uart1) is used anywhere in v3; all serial is
USB-CDC.

This is **Increment 1** of a two-step plan agreed with the maintainer:

1. **Inc 1 (this spec) — Passthrough:** a transparent host↔target serial
   bridge over CDC3. This is the base.
2. **Inc 2 (future, separate spec) — Sniffer:** a passive 2×RX full-duplex
   tap of an external UART link, with framing + per-channel timestamps,
   built on top of this base. **Out of scope here.**

The original user intuition — "two RXs" — belongs to Inc 2 (a passive tap
captures both data wires of an A↔B link). Inc 1 deliberately builds the
simpler transparent bridge first so the PIO-UART, CDC3 wiring, and
bus-lock plumbing exist and are smoke-tested before the sniffer layers on.

## Goals

- Transparent full-duplex serial bridge: a normal terminal (`minicom`,
  `screen`, `pyserial`) opens CDC3 and talks to the target as if wired
  directly. **No framing** on the data path.
- Baud, pins, and enable/disable controlled from the **CDC2 shell**
  (where `scan` / `swd` / `jtag` verbs already live).
- 8N1, configurable baud (default 115200), default pins GP4=TX / GP5=RX on
  the scanner header (level-shifted via TXS0108), respecting the existing
  `swd_bus_lock`.
- Leave room for Inc 2: claim only `pio1/SM1` (TX) + `pio1/SM2` (RX),
  leaving `SM3` free for the sniffer's second RX.

## Non-Goals (explicitly Inc 2 or later)

- Second RX channel, passive tap topology.
- Framing, per-channel tags, microsecond timestamps, host-side
  conversation reconstruction.
- Auto-baud detection.
- Configurable parity / stop bits / word length (Inc 1 is fixed 8N1).
- Monitoring/mirroring the bridge traffic to another CDC.

## Architecture

New service `services/target_serial/`, following the house pattern of
splitting **host-testable logic** from the **thin PIO hardware layer**
(mirrors `services/swd_core/`: `swd_phy` logic vs PIO wiring).

### Modules

- **`target_serial.{c,h}`** — pure state + logic, testable with
  `tests/hal_fake`:
  - State machine: `TARGET_SERIAL_DISABLED ⇄ TARGET_SERIAL_ENABLED`.
  - Config struct: `{ uint8_t tx_gp; uint8_t rx_gp; uint32_t baud; }`.
  - `baud → PIO clk_div` computation (integer divider; the TX program is
    8 cycles/bit, the RX program 8× oversample, so
    `div = sysclk / (baud * 8)`).
  - Pin validation (both pins in 0..7 scanner range, tx ≠ rx).
  - Owns no hardware directly; calls into `target_serial_pio` and
    `swd_bus_lock` via the same indirection the rest of the tree uses.

- **`target_serial_pio.{c,h}`** — thin layer over `hal/pio`:
  - Loads the two PIO programs, claims `pio1/SM1` (TX) and `pio1/SM2`
    (RX), configures pins/clkdiv, enables/disables the SMs.
  - `try_put` (TX) / `try_get` (RX) byte helpers, non-blocking.
  - Mirrors how `swd_phy.c` wraps `hal/pio` for `pio1/SM0`.

- **PIO programs** — inline `uint16_t` instruction arrays in the header
  (same convention as `emfi_pio`, `crowbar_pio`, `swd_phy` — **no `.pio`
  files / pioasm**; the tree has none):
  - `uart_tx`: standard PIO UART TX, 8 cycles/bit, start + 8 data + stop.
  - `uart_rx`: standard PIO UART RX, 8× oversample, start-bit detect +
    sample-at-center, pushes one byte per frame to the RX FIFO.
  - Both are the well-known RP2040 pio-uart examples (BSD-licensed
    reference; reimplemented, not copied from the unlicensed faultier).

### PIO allocation (after Inc 1)

| PIO | SM | Owner |
|-----|----|-------|
| pio0 | 0 | EMFI glitch (F4) |
| pio0 | 1 | Crowbar glitch (F5) |
| pio1 | 0 | SWD phy (F6) |
| pio1 | 1 | **target_serial UART TX (Inc 1)** |
| pio1 | 2 | **target_serial UART RX (Inc 1)** |
| pio1 | 3 | *free — reserved for sniffer 2nd RX (Inc 2)* |

## Data Flow (transparent, no framing)

```
Host (minicom on CDC3) ─► pump_target_cdc() ─► PIO SM1 TX ─► GP_TX ─► target
target ─► GP_RX ─► PIO SM2 RX ─► pump_target_cdc() ─► CDC3 ─► Host
```

`pump_target_cdc()` in `apps/faultycat_fw/main.c` (twin of
`pump_shell_cdc`, wired identically into the main loop and the nested
yield-pumps):

- If state is `DISABLED`: do nothing (CDC3 inert — no echo, no traffic).
- If `ENABLED`:
  1. Read up to 64 B from CDC3 (`usb_composite_cdc_read(USB_CDC_TARGET,...)`),
     `try_put` each byte to the TX FIFO. If the TX FIFO is full, drop the
     remainder (transparent bridge semantics; backpressure is the host's
     problem, matching real USB-serial adapters).
  2. Drain the RX FIFO with a **bounded spin (~10k iters, fall-through)**
     per the `pio_spin_must_be_bounded` rule — never an unbounded
     `try_get` loop that could starve `tud_task()` and brick magic-baud
     BOOTSEL recovery. Accumulate into a local buffer, write to CDC3.

## Control Plane (CDC2 shell)

New `serial` verb group in the CDC2 shell dispatcher, alongside
`swd` / `jtag` / `scan` / `bp` / `serprog`:

| Verb | Action |
|------|--------|
| `serial init [tx_gp] [rx_gp] [baud]` | Acquire `swd_bus_lock`; validate pins; configure + enable PIO UART. Defaults: `tx_gp=4`, `rx_gp=5`, `baud=115200`. Fails cleanly (prints error) if the bus is held by scanner/swd/jtag/campaign, or pins invalid, or already enabled. |
| `serial baud <n>` | Retune `clk_div` live via `hal_pio_sm_set_clkdiv_int` without deinit/init. Only valid while `ENABLED`. |
| `serial deinit` | `ENABLED → DISABLED`; disable SMs; release `swd_bus_lock`. |
| `serial status` | Print `state / tx_gp / rx_gp / baud`. |

CDC3 carries **only data bytes**, never control. This keeps `minicom`
plug-and-play and avoids in-band escape sequences.

## Integration & Safety Notes (from maintainer memory)

- **Echo-loop fix** (`feedback_usb_cdc_echo_loop`): remove CDC3 from the
  `for (i = 3; i < USB_CDC_COUNT; i++) echo_cdc(i)` loop in
  `usb_composite_task` (the loop becomes empty / is deleted). The bridge
  is pumped from `main.c` instead, so reading CDC3 in `usb_composite_task`
  would race the pump and steal bytes.
- **`tud_cdc_line_coding_cb` stays as-is** (`usb_composite.c:19`): it only
  traps the magic 1200-baud → BOOTSEL. Inc 1 does **not** read CDC3's
  line-coding for baud (baud comes from the shell), so there is zero risk
  of breaking the BOOTSEL recovery path. (Note for Inc 2: if we ever want
  CDC3 line-coding to drive baud, it must still honor the 1200 trap.)
- **Bus-lock** (`swd_bus_lock`): the passthrough is just another GP0–GP7
  consumer. `serial init` participates in the same priority arbitration as
  scanner/swd/jtag/campaign.
- **Bounded PIO spins** (`pio_spin_must_be_bounded`): the RX drain caps at
  ~10k iterations with NO-TARGET fall-through.
- **Static pump buffers** (`pump_reply_static`): any buffer in
  `pump_target_cdc` that could be large is `static`, since the pump is
  reachable from deep nested yield loops.
- **Ext-trigger coupling** (`ext_trigger_needs_jumper`): not directly
  relevant (no glitching here), but physical smoke should avoid firing
  HV/crowbar while wires are attached to the scanner header.

## Testing

- **Host-side unit tests** (`tests/test_target_serial.c`, with
  `hal_fake`):
  - State transitions: `init` enables, `deinit` disables, double-`init`
    rejected.
  - `baud → clk_div` math across {9600, 115200, 921600}.
  - Pin validation: out-of-range / tx==rx rejected.
  - `deinit` releases the bus-lock; `init` fails when lock held.
  - `baud` verb only valid while enabled.
- **Physical smoke** on the v2.2 board (per `smoke_before_tag`):
  - GP4/GP5 wired to a USB-TTL adapter; loopback + echo via `minicom` at
    115200 and one other baud.
  - Run the golden + regression checklist; report the result table; wait
    for maintainer before tagging. Surface empirical fixes as polish
    **before** the tag.

## Documentation

Update at phase close (per `docs_live_update`):
- `docs/ARCHITECTURE.md`: mark CDC3 "Target UART" as implemented; update
  the PIO allocation table (pio1/SM1+SM2 taken, SM3 reserved for sniffer).
- `docs/PORTING.md`: note the new `services/target_serial/` module and its
  PIO-UART programs.
- Add a short `docs/TARGET_SERIAL.md` (or a section) describing the shell
  verbs and the CDC3 data path.

## HV Safety

No change to HV drivers (`hv_charger` / `emfi_pulse` / `crowbar_mosfet`).
No signed safety checklist required for this work.

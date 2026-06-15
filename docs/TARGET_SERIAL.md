# Target Serial Passthrough (Increment 1)

A transparent host↔target UART bridge. CDC3 ("FaultyCat Target UART")
carries raw bytes; open it with any terminal (`minicom`, `screen`,
`pyserial`) and you talk to the target as if wired directly.

This is **Increment 1** of a two-step plan. Increment 2 (a passive
2×RX sniffer of an external UART link, with framing + per-channel
timestamps) builds on this base and is not yet implemented — see
`docs/superpowers/specs/2026-05-31-target-serial-passthrough-design.md`.

## Wiring

Uses the scanner header (GP0–GP7, level-shifted via the TXS0108).
Defaults:

| Signal | Pin |
|--------|-----|
| TX (FaultyCat → target RX) | GP4 |
| RX (target TX → FaultyCat)  | GP5 |

8N1, default 115200 baud. Both pins are runtime-selectable (any two
distinct channels in GP0–GP7).

## Control (CDC2 scanner shell)

Configuration goes through the CDC2 shell — the same place `scan` /
`swd` / `jtag` verbs live. Data never mixes with control.

| Command | Effect |
|---------|--------|
| `serial init [tx_gp] [rx_gp] [baud]` | Acquire the bus, enable the bridge. Defaults `4 5 115200`. |
| `serial baud <n>` | Change baud live (no re-init). |
| `serial deinit` | Disable, release the bus + pins. |
| `serial status` | Show state / pins / baud. |

`serial init` fails (prints `SERIAL: ERR enable_failed`) if the
SWD/JTAG/scan bus is busy — it shares the scanner header via
`swd_bus_lock` — or if pins are outside 0..7, the two pins are equal,
or the bridge is already enabled. Run `serial deinit` before a
`scan swd`, and vice-versa.

## Data path

```
Host (CDC3) ─► pump_target_cdc() ─► PIO SM1 (TX) ─► GP_TX ─► target
target ─► GP_RX ─► PIO SM2 (RX) ─► pump_target_cdc() ─► CDC3 ─► Host
```

No framing — bytes pass through untouched. The bridge is pumped from
the top-level main loop in `apps/faultycat_fw/main.c`. PIO uses
`pio1/SM1` (TX) and `pio1/SM2` (RX); `pio1/SM3` is reserved for the
Increment-2 sniffer's second RX.

## Implementation

- `services/target_serial/target_serial.c` — state machine, baud→PIO
  divider math (`125 MHz / (baud × 8)`, round-to-nearest, clamped to
  the RP2040's 16-bit clkdiv field), pin validation, `swd_bus_lock`
  arbitration (`SWD_BUS_OWNER_SERIAL`). Host-testable; no TinyUSB.
- `services/target_serial/target_serial_pio.c` — thin `hal/pio` layer:
  two hand-encoded UART PIO programs (from pico-examples, BSD-3),
  SM claim/config, byte put/get. RX drain is bounded so a flood can
  never starve `tud_task()` / the magic-baud BOOTSEL recovery path.
  - **TX program loops via WRAP — the SM config MUST set it.** The
    `uart_tx` program (4 instr: `pull`/`set`/`out`/`jmp x--`) has no
    explicit jump back to `pull`; between bytes it relies on the SM
    wrapping from the last instruction to the first. `hal_pio_sm_configure`
    only applies wrap when `wrap_end > wrap_target`, so the `txc` config
    sets `wrap_target = 0`, `wrap_end = TS_TX_PROG_LEN - 1`. The RX
    program instead self-loops with an explicit `jmp 0`, so it needs no
    wrap. **History (2026-06-15):** these wrap fields were initially
    unset, leaving the SDK default wrap `(0,31)`; the TX SM ran off the
    end of its 4-instruction program after the *first* byte and never
    transmitted a second. Symptom on a GP4↔GP5 loopback: exactly one
    byte round-trips, then stall — baud- and pull-independent. It looked
    like the TXS0108E level-shifter at first; the giveaway was sampling
    the CDC2 `SCAN` heartbeat during a transmit and seeing GP4 never
    toggle low, i.e. the TX line was idle, not a HW issue. Fixed by
    setting the wrap. Isolated/gapped single-byte loopback is now 20/20
    at 115200 and 9600.

## Notes

- **Host→target flow control.** `pump_target_cdc` only pulls bytes off
  CDC3 as fast as the 4-deep PIO TX FIFO drains; the byte that doesn't
  fit is stashed and retried, and no more are read until it does. Excess
  bytes therefore stay in CDC3's USB RX buffer and the host blocks
  (proper backpressure) instead of being dropped — a bulk paste at any
  baud arrives intact. (Earlier revisions dropped on TX-FIFO-full; that
  is no longer the case.)
- **RX throughput depends on how often the bridge is pumped.** The PIO
  RX FIFO is only 4 deep and the RX SM stalls (dropping bytes off the
  wire) once it fills. The main loop's cooperative-sleep therefore drains
  the bridge every ~100 us, not once per ~20 ms iteration — otherwise
  sustained target→host RX caps at ~1200-2400 baud. With the 100 us drain,
  target→host is loss-free at 115200 (verified on v2.2 with an FT232 at
  3V3, 64-byte bursts, every baud 1200..115200).
- The 1200-baud BOOTSEL escape still works on every CDC, including
  CDC3 — `tud_cdc_line_coding_cb` is unchanged.
- Baud is set via the shell, **not** via CDC3's line-coding. Opening
  CDC3 at an arbitrary baud in your terminal does not change the wire
  rate (only `serial init` / `serial baud` do).

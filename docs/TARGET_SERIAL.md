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

## Notes

- **No host→target flow control.** The host→target direction has no
  backpressure: if the PIO TX FIFO is full (host writing faster than
  the target baud drains, e.g. a paste at 9600), excess bytes are
  dropped — the same behavior as a plain USB-serial adapter. Interactive
  use never hits this; bulk transfers at low baud can.
- The 1200-baud BOOTSEL escape still works on every CDC, including
  CDC3 — `tud_cdc_line_coding_cb` is unchanged.
- Baud is set via the shell, **not** via CDC3's line-coding. Opening
  CDC3 at an arbitrary baud in your terminal does not change the wire
  rate (only `serial init` / `serial baud` do).

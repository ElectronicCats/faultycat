# drivers/voltage_mux — stub (not implemented on v2.x)

This directory is an **architectural placeholder**. The FaultyCat v3
plan (see [`FAULTYCAT_REFACTOR_PLAN.md`](../../FAULTYCAT_REFACTOR_PLAN.md)
§3) reserves a driver slot for a voltage multiplexer IC, following the
pattern that `hextreeio/faultier` uses on its own hardware.

## Why it is a stub on v2.x

FaultyCat HW v2.2 has **no voltage mux** component. The two crowbar
paths available in firmware —

- `LP` (low-power path, GP16)
- `HP` (N-MOSFET real voltage-glitch path, GP17)

— are selected by software toggling the respective gate GPIO through
`drivers/crowbar_mosfet`. There is no analog switch between paths and
no third-party mux IC to configure.

The legacy `firmware/c/glitcher/glitcher.c` defines `PIN_MUX0`,
`PIN_MUX1`, `PIN_MUX2` as GP1 / GP2 / GP3, but those are **upstream
faultier relics** that collide with the v2.x scanner channels and are
deliberately **not** ported to v3 (see `docs/PORTING.md` and
`docs/HARDWARE_V2.md` §4 *Known legacy quirks*).

Confirmed by the maintainer (Sabas, Electronic Cats) on 2026-04-23.

## Why keep the directory at all

- Preserves the architectural slot so a future v3.x hardware (if it
  grows a mux IC) maps to an obvious place without restructuring the
  tree.
- `voltage_mux.h` carries a hard `#error` so any accidental include
  is caught at compile time with a clear message, not a link error.
- Visibility: new contributors reading the layered tree see a driver
  slot labelled "voltage_mux" and this README explains why it's empty
  without them having to grep git history.

## How to lift the stub

If / when a hardware revision adds a mux IC:

1. Replace `voltage_mux.h` with the real API (following the shape of
   other drivers under `drivers/`).
2. Add `voltage_mux.c`, `CMakeLists.txt`, and update the parent
   `drivers/CMakeLists.txt` to `add_subdirectory(voltage_mux)`.
3. Add `tests/test_voltage_mux.c`.
4. Update `docs/HARDWARE_V2.md` (new pin assignments) and
   `docs/PORTING.md` (no legacy to port — it's new).
5. Delete this README and the `#error` line.

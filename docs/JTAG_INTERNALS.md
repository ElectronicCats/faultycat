# FaultyCat v3 — JTAG / scanner internals (F8)

This document describes how F8's services (`jtag_core`, `pinout_scanner`,
`buspirate_compat`, `flashrom_serprog`) are layered, what wire
protocols they speak, and the corner cases they were tuned against.
For the high-level service tree see
[`ARCHITECTURE.md`](ARCHITECTURE.md); for the legacy port mapping see
[`PORTING.md`](PORTING.md); for HW pinout caveats see
[`HARDWARE_V2.md`](HARDWARE_V2.md).

## 1. Service stack

```
┌──────────────────────────────────────────────────────────────┐
│  apps/faultycat_fw/main.c                                    │
│    process_shell_line — text-mode dispatcher (CDC2)          │
│    pump_shell_cdc     — routes bytes to active mode parser   │
│    s_shell_mode       — TEXT / BUSPIRATE / SERPROG           │
└────────────┬─────────────────────────────┬───────────────────┘
             │                             │
             ▼                             ▼
┌────────────────────────────┐   ┌──────────────────────────┐
│  services/buspirate_compat │   │  services/flashrom_serprog│
│  (BPv1 BBIO + OOCD JTAG)   │   │  (serprog v1 + 4-pin SPI) │
└────────────┬───────────────┘   └────────────┬─────────────┘
             │                                │
             ▼                                ▼
┌────────────────────────────┐   ┌──────────────────────────┐
│  services/jtag_core        │   │  4-pin CPU SPI bit-bang  │
│  (CPU bit-bang TAP +       │   │  (in main.c bridge cbs)  │
│   IDCODE chain)            │   │                          │
└────────────┬───────────────┘   └────────────┬─────────────┘
             │                                │
             └────────────┬───────────────────┘
                          ▼
            ┌──────────────────────────────┐
            │  drivers/scanner_io / hal/gpio│
            │  (8 channels, GP0..GP7)       │
            └──────────────────────────────┘
```

`services/pinout_scanner` calls `jtag_core` and `swd_phy` (F6) under a
permutation iterator; it doesn't sit in the wire stack itself.

## 2. JTAG TAP state machine (jtag_core)

`services/jtag_core` is a CPU bit-bang TAP controller — adapted in
function shape from [blueTag@v2.1.2](../third_party/blueTag/) under
MIT, reimplemented against `hal/gpio` so the v3 layered model holds.

### Pin assignment

JTAG over the v2.x scanner header (Conn_01x10, GP0..GP7). Operator
picks 4 of the 8 channels at session start; F8-2's
`pinout_scanner` auto-discovers when the pinout is unknown.

```
TDI  → host drives, target latches on TCK rising edge   (push-pull, unidirectional host→target)
TMS  → host drives, target latches on TCK rising edge   (push-pull, unidirectional host→target)
TCK  → host drives                                       (push-pull, unidirectional host→target)
TDO  → target drives, host samples during TCK high       (push-pull, unidirectional target→host)
TRST → optional, host drives active-low                  (push-pull, unidirectional host→target)
```

**Why JTAG works through the TXS0108E and SWD doesn't.** All four (or
five) JTAG signals are *unidirectional push-pull at any given moment*
— only one endpoint actively drives a line. The TXS0108E's
auto-direction sniff handles those cleanly. The bidirectional bug
that breaks SWD on this board (host and target both drive SWDIO at
different times during the ACK window) does not apply. See
[`HARDWARE_V2.md §2`](HARDWARE_V2.md) for the full SWD analysis.

### Timing model (CPU bit-bang)

The bit-bang loop is two `hal_gpio_put` calls and one `hal_gpio_get`
per TCK cycle:

```c
hal_gpio_put(tck, true);          // rising edge — target latches MOSI/TMS/TDI
bool tdo = hal_gpio_get(tdo);     // host samples during TCK high
hal_gpio_put(tck, false);         // falling edge
```

At ~5 MHz native CPU GPIO toggle, a TAP_SHIFT at 8 kbits sustains
~1.6 ms total. The TXS0108EPW datasheet caps push-pull at 40 Mbps so
we're well under wire-rate limits. No PIO needed; the cooperative
main loop schedules TinyUSB and glitch campaigns between bytes.

### State machine — supported transitions

`services/jtag_core` exposes high-level helpers that wrap
IEEE 1149.1 TAP transitions:

| Function | Transition | Notes |
|---|---|---|
| `jtag_reset_to_run_test_idle` | any → Test-Logic-Reset → Run-Test/Idle | 5×TMS=1 then 1×TMS=0. Always safe. |
| `enter_shift_dr` *(static)* | Run-Test/Idle → Shift-DR | TMS sequence 1,0,0. |
| `enter_shift_ir` *(static)* | Run-Test/Idle → Shift-IR | TMS sequence 1,1,0,0. |
| `exit_shift_to_run_test_idle` *(static)* | Shift-{DR,IR} → Run-Test/Idle | TMS sequence 1,1,0. |
| `jtag_assert_trst` | brief TRST low pulse | 1 ms; no-op if no TRST. |
| `jtag_clock_bit` | shift one bit | Used by `services/buspirate_compat`. |

### Chain detection (jtag_detect_chain_length)

blueTag-style: fill every IR with all-1s (BYPASS instruction), then
shift a 0 through the 1-bit BYPASS DRs and count clocks until it
appears at TDO. Returns 0..32; 0 = no target / floating bus.

### IDCODE chain readout (jtag_read_idcodes)

After TAP reset the default DR is IDCODE for IEEE 1149.1 compliant
devices. We:

1. Detect chain length first (above).
2. Walk to Shift-DR with TDI=1.
3. For each device, clock 32 bits with TMS=0; accumulate MSB-first
   (so wire-LSB lands in the high bit), then `jtag_bit_reverse32` to
   produce the canonical IDCODE.
4. Validate via `jtag_idcode_is_valid` per IEEE 1149.1.

### Validity rules (jtag_idcode_is_valid)

```
bit 0       must be 1
mfg id [11:1]  in 1..126                  (rejects 0 and reserved)
bank   [11:8]  ≤ 8                        (clamps to JEP106 known banks)
value       not 0 and not 0xFFFFFFFF      (rejects all-floating sentinels)
```

This admits about half of the 32-bit space; on its own it's not
strict enough to filter bus noise — see §3.

## 3. Pinout scanner — false-positive guard (F8-6)

The brute-force scanner iterates P(8,4) = 1680 candidate JTAG
pinouts and P(8,2) = 56 candidate SWD pinouts. For each it calls
`jtag_init` + `jtag_read_idcodes` (or `swd_phy_init` + `swd_dp_connect`)
and checks the result against the validator.

### Empirical observation (F8-6 smoke, 2026-04-28)

With a Pi Pico wired to the FaultyCat scanner header (powered on,
running its own firmware — not as a target, just as a parasitic load),
F8-1's scanner produced false matches:

```
SCAN: jtag MATCH tdi=GP2 tdo=GP1 tms=GP3 tck=GP4
SCAN:   chain=1 idcode[0]=0x6B5AD5AD
```

`0x6B5AD5AD` passes the validator (bit 0 = 1, mfg id = 86 ∈ [1..126],
bank = 5 ≤ 8) but RP2040 has no JTAG TAP — that IDCODE was bus noise
forwarded back through the TXS0108E auto-direction logic. About half
of the 32-bit space passes the validator on its own.

### Mitigation — 3-read consistency check

`services/pinout_scanner` re-reads the IDCODE chain (or DPIDR) two
more times after the initial validator pass. Real silicon is
deterministic — three identical reads in a row is virtually
guaranteed. Pseudo-random bus noise with a non-deterministic source
rarely produces three identical reads.

The check fires only on a candidate match (i.e. when the validator
already accepted), so the no-match scan path stays at full speed
(1680 perms in ~12 s).

```c
#define PINOUT_SCAN_CONFIRM_READS 2u  // initial + this many re-reads must match
```

If you ever need to tune this for a specific noisy environment, raise
the constant in `services/pinout_scanner/pinout_scanner.c` — there's
no runtime knob.

## 4. BusPirate v1 binary mode (buspirate_compat)

`services/buspirate_compat` implements the subset of the BusPirate
v1.x binary protocol that OpenOCD's `interface/buspirate.cfg` actually
exercises. Adapted from blueTag's `src/modules/openocd/openocdJTAG.c`
under MIT (attribution at file head). Streaming, not buffered — the
upstream `getc(stdin)` + 4 KB stage buffer is gone.

### Wire protocol

**BBIO entry** (host → device, 1 byte each, until reply seen):

| Byte | Reply | Meaning |
|---|---|---|
| 0x00..0x05, 0x07..0x0E | "BBIO1" (5 B) | reset / unsupported sub-mode → fall back to BBIO |
| 0x06 | "OCD1" (4 B) | enter OpenOCD JTAG sub-mode |
| 0x0F | invokes `on_exit` cb | reset to user terminal — the only escape |

OpenOCD's probe loop sends 0x00 ×25 expecting "BBIO1" anywhere in the
reply stream; we reply once per zero byte.

**OOCD sub-commands** (after 0x06 → "OCD1" entry):

| Cmd | Args (host→dev) | Reply (dev→host) | Notes |
|---|---|---|---|
| 0x00 | none | "BBIO1" + state ← BBIO_IDLE | "go back" |
| 0x01 PORT_MODE | 1 byte | (none) | unsupported, eaten |
| 0x02 FEATURE | 2 bytes | (none) | unsupported, eaten |
| 0x03 READ_ADCS | none | [0x03, 8, 0×8] | always reports 0 — no ADC exposed in OOCD |
| 0x05 TAP_SHIFT | 2-byte BE bit count + 2*ceil(N/8) interleaved (TDI, TMS) bytes | [0x05, hi, lo] + ceil(N/8) TDO bytes | the workhorse — see below |
| 0x06 ENTER_OOCD | none | "OCD1" | re-enter (idempotent) |
| 0x07 UART_SPEED | 3 bytes | [0x07, 0x00] | unsupported, eaten + ack |
| 0x08 JTAG_SPEED | 2 bytes | (none) | unsupported, eaten |
| any other | none | [0x00] | catch-all NAK |

Length clamp on TAP_SHIFT: 0x2000 bits (8192) — inherited from
upstream. Anything larger is silently truncated to that bound; our
reply header echoes the *effective* length so the host can detect.

### Streaming TAP_SHIFT

Upstream blocks in `getc(stdin)` and stages 2 * ceil(N/8) bytes (up
to 4 KB) before processing. We don't have that latency budget — the
glitch campaigns and TinyUSB share the same cooperative loop. So:

1. Receive the 2-byte length, echo `[0x05, hi, lo]` immediately.
2. As each (TDI, TMS) byte pair arrives, clock 8 (or fewer for the
   tail) bits via `jtag_clock_bit`, accumulate TDO bits LSB-first into
   one output byte, emit it inline. No stage buffer.

### Mode-switch trailing-byte fix (F8-6)

When the operator types `buspirate enter\r\n`, `process_shell_line`
fires on the `\r`, the mode flips to `BUSPIRATE`, and the `\n` byte
is the next iteration of the pump loop. Without protection, that
`\n = 0x0A` lands in the BBIO state machine's default branch and
emits a spurious "BBIO1" reply to the host's first 0x00.

`pump_shell_cdc` now breaks out of the per-batch loop the moment a
`process_shell_line` call flips the mode away from TEXT. The next
pump cycle starts clean with the binary parser owning every host byte.

### Disconnect detection

`buspirate_compat` has its own escape (0x0F) but a crashed OpenOCD
won't send it. `apps/faultycat_fw/main.c` watches the connect→disconnect
edge on CDC2 and fires `bp_on_exit_cb` (or `sp_on_exit_cb` for
serprog) so the next session starts with `jtag_core` released.

## 5. Serial Flasher Protocol — flashrom_serprog

`services/flashrom_serprog` implements [serprog v1](https://www.flashrom.org/supported_hw/supported_prog/serprog/serprog-protocol.html)
over CDC2 + a 4-pin CPU SPI bit-bang. Adapted from blueTag's
`src/modules/flashProgrammer/serProg.c` under MIT.

### Supported subset

| Cmd | Code | Reply | Notes |
|---|---|---|---|
| NOP | 0x00 | S_ACK | sanity ping |
| Q_IFACE | 0x01 | S_ACK + 0x0001 LE | serprog v1 |
| Q_CMDMAP | 0x02 | S_ACK + 32 bytes | bits 0..5 in byte 0 + bits 0,2,3,4,5 in byte 2 — exactly the supported subset |
| Q_PGMNAME | 0x03 | S_ACK + "FaultyCat" + NULs (16 B) | informational |
| Q_SERBUF | 0x04 | S_ACK + 0xFFFF LE | host-buf size we always handle |
| Q_BUSTYPE | 0x05 | S_ACK + 0x08 | SPI-only |
| SYNCNOP | 0x10 | S_NAK + S_ACK | resync |
| S_BUSTYPE | 0x12 | S_ACK iff SPI bit set | host requests bus mask |
| O_SPIOP | 0x13 | (see below) | the workhorse |
| S_SPI_FREQ | 0x14 | S_ACK + actual freq LE | we always report 1 MHz (bit-bang has no programmable clock) |
| S_PIN_STATE | 0x15 | S_ACK | no-op |
| anything else | — | S_NAK | flashrom skips |

### O_SPIOP wire format

Host → device:

```
0x13 | wlen[2:0] LE | rlen[2:0] LE | wbyte_0 wbyte_1 ... wbyte_(wlen-1)
```

Device flow:

```
1. CS asserted low.
2. For each wbyte: shift 8 SCK cycles, MOSI = byte bit MSB-first,
   MISO sampled but discarded (write phase).
3. S_ACK emitted on the wire.
4. For each of rlen bytes: shift 8 SCK cycles, MOSI = 0,
   MISO sampled and accumulated MSB-first → emitted on the wire.
5. CS deasserted high.
```

A yield callback fires every 128 bytes of SPIOP traffic so a
multi-MB chip read keeps `tud_task` and the glitch campaigns alive.

### SPI mode

Mode 0 (CPOL=0, CPHA=0), MSB-first per the 25-series convention. The
inline bit-bang in `apps/faultycat_fw/main.c::sp_xfer_byte_cb`:

```c
for (int bit = 7; bit >= 0; bit--) {
    hal_gpio_put(s_sp_pin_mosi, (bool)((out >> bit) & 1u));
    hal_gpio_put(s_sp_pin_sck,  true);   // rising — target latches
    if (hal_gpio_get(s_sp_pin_miso)) in |= (uint8_t)(1u << bit);
    hal_gpio_put(s_sp_pin_sck,  false);
}
```

Default pinout (overridable via `serprog enter <cs> <mosi> <miso> <sck>`):

```
CS    → BOARD_GP_SCANNER_CH0 (GP0)   active-low, output
MOSI  → BOARD_GP_SCANNER_CH1 (GP1)   output
MISO  → BOARD_GP_SCANNER_CH2 (GP2)   input + internal pull-up
SCK   → BOARD_GP_SCANNER_CH3 (GP3)   output, idle-low
```

MISO pull-up is critical: with no chip attached, a floating bus would
otherwise produce random data; pull-up forces a stable 0xFF (the
"no-chip" 25-series signature) so flashrom's chip-detect step fails
gracefully.

### Disconnect detection (only protocol exit)

The serprog spec defines no exit byte. F8-5's only way out is for
flashrom to disconnect (DTR drop on CDC2). `apps/faultycat_fw/main.c`
watches the connect→disconnect edge and fires `sp_on_exit_cb` which
releases the SPI pins and flips back to text shell.

## 6. Mutual exclusion — scanner pins

GP0..GP7 are shared between five potential consumers:

```
drivers/scanner_io      ← passive read-all, no claim during F8 sessions
services/swd_core       ← F6 — claims SWCLK/SWDIO/(nRST), F8-1 soft-lock
services/jtag_core      ← F8-1 — claims TDI/TDO/TMS/TCK/(TRST)
services/pinout_scanner ← F8-2 — claims via jtag_core/swd_phy per perm
services/buspirate_compat   ← F8-4 — uses jtag_core under the hood
services/flashrom_serprog   ← F8-5 — claims CS/MOSI/MISO/SCK directly
```

F8 enforces this with a **shell-level soft-lock**:

- `swd init` while `jtag_is_inited()` → `SWD: ERR jtag_in_use`
- `jtag init` while `swd_shell_inited` → `JTAG: ERR swd_in_use`
- `scan jtag` / `scan swd` / `buspirate enter` / `serprog enter` all
  refuse if either of the above is held.

Each binary-mode service teardown (0x0F for BusPirate, DTR drop for
serprog) releases the pins so the next session starts with `swd_phy`
or `jtag_core` free to claim again.

F9 promotes this soft-lock to a formal `pico-sdk mutex_t` covering
`daplink_usb` (CMSIS-DAP from external host) and the glitch-campaign
SWD verification path too.

## 7. Test coverage

| Service | Test | Cases | What's tested |
|---|---|---|---|
| `jtag_core` | `tests/test_jtag_core.c` | 24 | bit-reverse, IDCODE validator, permutation count, init/deinit, TAP transition sequences via TCK edge sampler, chain detect 0/1/3 devices, IDCODE STM32F103 |
| `pinout_scanner` | `tests/test_pinout_scanner.c` | 13 | permutation iterator (P(n,k) total, lex order, dedup, edge cases). End-to-end scan deferred to physical smoke. |
| `buspirate_compat` | `tests/test_buspirate_compat.c` | 22 | BBIO entry + reset / OCD entry / OOCD subcmds / CMD_TAP_SHIFT 0/4/8/12 bits + max-length clamp / OpenOCD-like full session |
| `flashrom_serprog` | `tests/test_flashrom_serprog.c` | 25 | every supported query / S_BUSTYPE accept-reject / S_SPI_FREQ / SPIOP read-only/write-only/write+read JEDEC ID / yield throttling / handshake-then-RDID |

26 test binaries / 347 cases / 100% green.

## 8. Physical smoke results (2026-04-28, FaultyCat v2.2 + RP2040)

Per `apps/faultycat_fw/faultycat.uf2` from `rewrite/v3` HEAD:

| Check | Result |
|---|---|
| USB enumeration (`1209:fa17`, 4×CDC) | ✓ |
| CDC2 unified shell + help | ✓ |
| F8-1 jtag init/chain/idcode/deinit (no target) | ✓ chain=0, ERR no_target |
| Soft-lock SWD↔JTAG bidirectional | ✓ |
| F8-2 `scan jtag` (clean bus) | ✓ NO_MATCH after 1680 perms |
| F8-2 `scan jtag` (RP2040 wired, parasitic noise) | **fixed by F8-6 stability check** — initially produced false IDCODE 0x6B5AD5AD; consistency check rejects |
| F8-2 `scan swd` (clean bus) | ✓ NO_MATCH after 56 perms |
| F8-4 BusPirate handshake (BBIO1 + OCD1 + 0x0F exit) | ✓ exact 25-byte BBIO1×5 reply post-F8-6 mode-switch fix |
| F8-5 serprog NOP/Q_PGMNAME/Q_BUSTYPE/SYNCNOP/S_BUSTYPE/Q_CMDMAP/SPIOP read-only | ✓ all reply bytes exact |
| F8-5 disconnect detection (DTR drop) | ✓ |
| F4 EMFI ping (regression) | ✓ `PONG F4` |
| F5 crowbar ping (regression) | ✓ `PONG F5` |
| F3 magic-baud BOOTSEL (regression) | ✓ |
| Re-flash post-BOOTSEL | ✓ |

**Not verified physically** (need external targets):
- JTAG IDCODE against an STM32 / ESP32 / nRF target.
- OpenOCD via BusPirate end-to-end against any JTAG MCU.
- flashrom against a 25-series SPI flash chip.
- `scan swd` against an SWD target — also blocked by F6's
  TXS0108E-bidirectional gate, see `HARDWARE_V2.md §2`.

## 9. Future work (post-F8)

- **F7 — CMSIS-DAP daplink_usb**: parser for the Vendor IF + HID v1
  bulk endpoints. F8-1's `jtag_clock_bit` will plug straight in for
  the JTAG side; F6's `swd_dp` for the SWD side. Blocked on F6
  HW gate for SWD validation.
- **F9 — campaign manager + mutex_t**: promote the F8-1 soft-lock to
  a formal mutex covering daplink_usb + glitch-engine SWD verification.
- **TXS0108E HW bypass / replacement**: a board rev that swaps the
  TXS0108EPW for either direct-bypass (3.3 V both sides) or
  74LVC1T45 with explicit DIR control unblocks F6 SWD physically,
  which in turn unblocks `scan swd` and the SWD half of CMSIS-DAP.
- **Stricter IDCODE validator**: optional JEP106 manufacturer-table
  check (blueTag carries `jep106.inc`). Would tighten the false-positive
  rejection further beyond the F8-6 consistency check, at the cost of
  ~10 KB of vendor-name strings.

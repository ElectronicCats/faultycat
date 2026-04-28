# FaultyCat v3 — campaign manager + SWD bus mutex (F9)

This document describes how F9's services (`swd_bus_lock`,
`campaign_manager`, `host_proto/campaign_proto`) interlock, what wire
protocols they speak, and the state machines + ownership contract
they enforce. For the F8 wire stack (JTAG, scanner, BusPirate,
serprog) see [`JTAG_INTERNALS.md`](JTAG_INTERNALS.md); for the
high-level service tree see [`ARCHITECTURE.md`](ARCHITECTURE.md).

## 1. Service stack

```
┌──────────────────────────────────────────────────────────────────┐
│  apps/faultycat_fw/main.c                                        │
│    campaign_dispatch_executor — picks engine adapter per step    │
│    campaign_executor_emfi / _crowbar — blocking-with-yield       │
│    pump_emfi_cdc / pump_crowbar_cdc — F9-4 dispatch              │
└────────────────────────┬─────────────────────────────────────────┘
                         │
        ┌────────────────┴───────────────────┐
        ▼                                    ▼
┌────────────────────────┐    ┌──────────────────────────────┐
│ services/campaign_     │    │ services/host_proto/         │
│ manager (F9-2)         │    │ campaign_proto (F9-4)        │
│                        │    │  • CONFIG / START / STOP /   │
│  IDLE→CONFIGURING→     │    │    STATUS / DRAIN opcodes    │
│  SWEEPING→DONE/STOPPED │    │  • engine implied by CDC     │
│                        │    │    (CDC0=EMFI, CDC1=crowbar) │
└────────────┬───────────┘    └──────────────────┬───────────┘
             │                                   │
             ▼                                   ▼
   ┌───────────────────┐         ┌──────────────────────────────┐
   │ services/swd_bus_ │         │ services/host_proto/         │
   │ lock (F9-1)       │         │ {emfi_proto, crowbar_proto}  │
   │  acquire/release  │         │ extended with CAMPAIGN_*     │
   │  4-state owner    │         │ opcodes 0x20..0x24            │
   └───────────────────┘         └──────────────────────────────┘
             │
             ▼
       (F6 swd_phy / F8-1 jtag_core
        coordinate via shell-level
        soft-lock as well; F9 mutex
        is the service-layer ledger)
```

## 2. SWD bus lock — service-layer mutex (`swd_bus_lock`)

### 2.1 Why a dedicated lock and not just shell soft-lock

F8-1 introduced a *shell-level* soft-lock between SWD and JTAG: the
`swd init` and `jtag init` commands refuse if the other is already
held. That covers the operator path on CDC2.

F9 introduces *service-level* contention: a campaign's post-fire
verify hook reads SWD; the F8-2 scanner walks SWD/JTAG; the future
F7 daplink_usb runs a CMSIS-DAP host. These callers don't go through
the shell, so the soft-lock can't see them. `swd_bus_lock` is the
ledger they all consult before touching the wire.

The two locks coexist:

| Layer | Caller | Mechanism | Catches |
|---|---|---|---|
| Shell soft-lock | F8-1 swd / jtag CDC2 commands | static `bool swd_shell_inited` + `jtag_is_inited()` checks in main.c | operator typing both commands |
| Service mutex (F9-1) | campaign / scanner / daplink | `swd_bus_lock` API | service-to-service contention |

The F9 mutex does NOT replace the F8-1 soft-lock — they sit at
different layers and answer different questions.

### 2.2 API

```c
typedef enum {
    SWD_BUS_OWNER_IDLE     = 0,
    SWD_BUS_OWNER_CAMPAIGN = 1,
    SWD_BUS_OWNER_SCANNER  = 2,
    SWD_BUS_OWNER_DAPLINK  = 3,
} swd_bus_owner_t;

void              swd_bus_lock_init(void);
bool              swd_bus_acquire(swd_bus_owner_t who, uint32_t timeout_ms);
bool              swd_bus_try_acquire(swd_bus_owner_t who);
void              swd_bus_release(swd_bus_owner_t who);
swd_bus_owner_t   swd_bus_owner(void);
bool              swd_bus_is_held(void);
```

### 2.3 Semantics

- **Single-owner**, no re-entrance. Same-owner double-acquire returns
  `false` rather than silently allowing it — that surface accidental
  double-locks at the call site.
- **Wrong-owner release is a silent no-op.** A buggy caller that
  passes the wrong tag doesn't cascade into mid-campaign corruption;
  it just fails to release. The diag layer can flag it.
- **`SWD_BUS_OWNER_IDLE` is not a valid acquirer**. "Acquire as nobody"
  is a programmer error — surface via `false`.
- **timeout_ms** modes:
  - `SWD_BUS_TIMEOUT_NONE` (= 0) → poll once, return immediately.
  - `SWD_BUS_TIMEOUT_FOREVER` (= 0xFFFFFFFF) → block until released.
  - any finite ms value → poll every ~1 ms via `hal_sleep_ms` so
    the cooperative main loop keeps running tud_task / glitch
    campaigns / etc.

### 2.4 Implementation

Single-core cooperative model: no IRQ-side acquires (F4/F5 PIO ISRs
never touch SWD), so a plain volatile flag with explicit owner tag is
enough. We don't link pico-sdk's `mutex_t` directly — keeps the host
test build trivial against `hal_fake`.

```c
static volatile bool             s_locked = false;
static volatile swd_bus_owner_t  s_owner  = SWD_BUS_OWNER_IDLE;
```

### 2.5 Static priority — call-site responsibility

Plan §4 specifies a static priority `campaign > scanner > daplink_host`.
F9-1's lock does NOT enforce it on its own — it's exclusion only.
The priority lives at the call sites:

- **CAMPAIGN** acquires inside `campaign_executor_*` (the dispatcher
  in `apps/faultycat_fw/main.c`) around the verify hook only. Holds
  the bus for milliseconds at most, releases between steps.
- **SCANNER** (F8-2) doesn't acquire today — its own
  jtag_init/swd_phy_init lifecycle on the scanner header pins
  predates the F9 mutex. It will adopt the lock in F-future when the
  campaign-vs-scanner concurrency case becomes reachable (today the
  shell-level dispatcher doesn't run them simultaneously).
- **DAPLINK** (F7) is the polite consumer. Per plan §4 #3, when it
  receives a host CMSIS-DAP command and `swd_bus_try_acquire(DAPLINK)`
  returns false, it MUST reply `DAP_ERROR (busy)` and the external
  host retries. F9 ships the contract; F7 lands the parser that
  honors it.

Because campaign and scanner are operator-driven and don't run
simultaneously in normal use, the FIFO-fair lock matches the priority
in practice without explicit preemption logic.

### 2.6 Test coverage

`tests/test_swd_bus_lock.c` — 13 cases:

- init, init clears held state.
- Acquire when idle, release returns to idle, re-acquire after
  release.
- Contention rejection (held bus → other owner gets false).
- No re-entrance (same-owner double-acquire rejected).
- `SWD_BUS_OWNER_IDLE` rejected as acquirer.
- Wrong-owner / double release safety.
- Each consumer tag can independently acquire.
- `swd_bus_try_acquire` is alias for `acquire(NONE)`.

## 3. Campaign manager state machine (`campaign_manager`)

### 3.1 States

```
                   ┌──────────────┐
            init   │              │
   ─────────────► │     IDLE     │ ◄────── stop()
                  │              │     (from any state)
                  └──────┬───────┘
                         │
                  configure(cfg)
                         │
                         ▼
                  ┌──────────────┐
                  │ CONFIGURING  │
                  └──────┬───────┘
                         │
                      start()
                         │
                         ▼
                  ┌──────────────┐    executor returns false
                  │   SWEEPING   │ ─────────────────────────► ERROR
                  └──────┬───────┘
                         │
            step_n >= total_steps
                         │
                         ▼
                  ┌──────────────┐
                  │     DONE     │
                  └──────────────┘

  STOPPED is a separate terminal — reachable from SWEEPING via stop().
  Tick() in any non-SWEEPING state is a silent no-op.
```

Only `IDLE → CONFIGURING` accepts a fresh config. Reconfigure mid-
sweep is rejected (returns false, state unchanged). Restart from
DONE / STOPPED / ERROR requires a fresh `configure()` first.

### 3.2 Public API

```c
void   campaign_manager_init(void);
bool   campaign_manager_configure(const campaign_config_t *cfg);
bool   campaign_manager_start(void);
void   campaign_manager_stop(void);
void   campaign_manager_tick(void);

void   campaign_manager_get_status(campaign_status_t *out);
size_t campaign_manager_drain_results(campaign_result_t *out, size_t max_n);

void   campaign_manager_set_step_executor(campaign_step_executor_t fn,
                                          void *user);
```

### 3.3 Sweep generator — cartesian, lex-ordered

`campaign_total_steps(cfg) = delay_count × width_count × power_count`.
Iteration order is `power innermost → width middle → delay
outermost` so the most-fluctuating axis cycles fastest. Engine-
specific physical preferences can override this in F-future.

`campaign_step_to_params(cfg, step, &delay, &width, &power)` is a
pure function — exposed for tests and for F9-4's wire-protocol
helpers that need to display planned param tuples.

Axis collapse (`step == 0`) yields exactly one value (the start);
lets the operator hold one or two axes constant without burning a
CONFIGURE round-trip just to set step.

### 3.4 Step executor — pluggable

```c
typedef bool (*campaign_step_executor_t)(uint32_t step,
                                         const campaign_config_t *cfg,
                                         uint32_t delay,
                                         uint32_t width,
                                         uint32_t power,
                                         uint8_t *out_fire_status,
                                         uint8_t *out_verify_status,
                                         uint32_t *out_target_state,
                                         void *user);
```

Default = `campaign_noop_executor` — zeros everything, returns true.
F9-2's host-side tests use this default to drive the state machine
without engine integration.

F9-3's `apps/faultycat_fw/main.c` registers a single dispatcher,
`campaign_dispatch_executor`, which picks `campaign_executor_emfi`
or `campaign_executor_crowbar` based on `cfg->engine`.

### 3.5 Engine adapters (F9-3)

Both adapters are blocking-with-cooperative-yield:

```
configure → arm → fire → poll {
    tick engine state machine
    if state == FIRED: success
    if state == ERROR: failure with engine err code
    yield: usb_composite_task + pump_emfi/crowbar_cdc
    sleep 1 ms
    if (now - start) > 10 s: timeout
}
verify hook (F-future SWD verify; today no-op):
    swd_bus_try_acquire(CAMPAIGN)
    call hook  ←  no-op until F6 unblocks SWD physically
    swd_bus_release(CAMPAIGN)
```

Each step takes ~50 ms settle + ~10 ms fire + verify ≤ 100 ms = ~160
ms typical. The yield + 1 ms sleep keeps TinyUSB and the host_proto
pumps fed during the wait.

Engine errors lift into `result.fire_status` as
`0x80 | engine_err_code` so the host can demux without parsing a
separate error stream.

### 3.6 Result record (28 B fixed-width)

```c
typedef struct {
    uint32_t step_n;
    uint32_t delay;
    uint32_t width;
    uint32_t power;
    uint8_t  fire_status;     // 0=ok, 0x80|engine_err on failure
    uint8_t  verify_status;   // 0=skipped, 0xFE=bus_busy, others reserved
    uint8_t  reserved[2];
    uint32_t target_state;    // verify hook may stash a u32 here
    uint32_t ts_us;
} __attribute__((packed)) campaign_result_t;
```

Plan §F9 D1 originally listed 24 B; per-field math gives 28 B with
the reserved[2] alignment slot. 28 fits comfortably within typical
CDC frame sizes and aligns to a 4-byte boundary so the host parser
can cast directly.

### 3.7 Ringbuffer

`CAMPAIGN_RESULT_RING_DEPTH = 256` entries × 28 B = ~7 KB. Single
producer (`campaign_manager_tick`), single drainer
(`campaign_manager_drain_results` from `host_proto/campaign_proto`).
Cooperative single-core, so plain head/tail without atomics.

Overflow drops the new result and bumps `results_dropped`. The host
can detect lag without blocking the sweep — exposed via `STATUS`.

### 3.8 Test coverage

`tests/test_campaign_manager.c` — 27 cases:

- Axis math: collapse on step==0, inclusive end, partial-step
  truncation, inverted range → 0, NULL safety.
- Total + step_to_params: cartesian, lex order, OOR rejection.
- State machine: init→idle, configure transitions, invalid config
  rejected, start without config rejected, tick advances to DONE,
  stop transitions to STOPPED, tick in IDLE no-op, reconfigure
  mid-sweep rejected.
- Ringbuffer: drain in order, partial drain leaves rest, overflow
  drops past depth.
- Custom executor: called once per step with correct params, status
  bytes propagate to result, false return → ERROR + result still
  pushed for diag, default no-op zeros everything.
- Result record size = 28 B exactly.

## 4. Wire protocol (`host_proto/campaign_proto`)

Multiplexed on CDC0 (EMFI campaigns) and CDC1 (crowbar campaigns).
Both protos use the same opcode layout via the shared
`campaign_proto` helper module — the engine is implied by which CDC
received the command.

### 4.1 Opcodes

| Cmd | Code | Args (host→dev) | Reply (dev→host) |
|---|---|---|---|
| CAMPAIGN_CONFIG | 0x20 | 40 B (10 × u32 LE) | 1 B status |
| CAMPAIGN_START  | 0x21 | — | 1 B status |
| CAMPAIGN_STOP   | 0x22 | — | 1 B status |
| CAMPAIGN_STATUS | 0x23 | — | 20 B status struct |
| CAMPAIGN_DRAIN  | 0x24 | 1 B max_count (1..18) | 1 B n + n × 28 B records |

Status codes in the 1-byte reply:
```
0x00  OK
0x01  ERR_BAD_LEN     // payload size mismatch
0x02  ERR_REJECTED    // campaign_manager API returned false
                      // (e.g. invalid config, restart while running)
```

### 4.2 CONFIG payload layout

```
offset  field
  0..3  delay_start  (u32 LE)
  4..7  delay_end    (u32 LE)
  8..11 delay_step   (u32 LE)
 12..15 width_start  (u32 LE)
 16..19 width_end    (u32 LE)
 20..23 width_step   (u32 LE)
 24..27 power_start  (u32 LE)
 28..31 power_end    (u32 LE)
 32..35 power_step   (u32 LE)
 36..39 settle_ms    (u32 LE)
```

### 4.3 STATUS reply layout

```
offset  field
  0     state            (u8 — campaign_state_t)
  1     err              (u8 — campaign_err_t)
  2..3  reserved
  4..7  step_n           (u32 LE)
  8..11 total_steps      (u32 LE)
 12..15 results_pushed   (u32 LE)
 16..19 results_dropped  (u32 LE)
```

### 4.4 DRAIN reply layout

```
offset  field
  0     n              (u8 — number of records that follow)
  1..   record_0       (28 B)
        record_1       (28 B)
        ...
```

`n` is capped at `CAMPAIGN_DRAIN_MAX_COUNT = 18` so the reply
(1 + 18 × 28 = 505 B) fits inside the 512-byte
`EMFI_PROTO_MAX_PAYLOAD` ceiling. F9-4 also bumped
`CROWBAR_PROTO_MAX_PAYLOAD` from 64 B to 512 B for the same reason
— the original 64 B cap silently rejected any DRAIN reply >64 B
inside `write_frame`'s defensive guard.

The host iterates DRAIN until `n == 0` to collect a longer sweep.

### 4.5 Engine implied by CDC

`emfi_proto.c` (CDC0) sets `cfg.engine = CAMPAIGN_ENGINE_EMFI` before
calling `campaign_proto_apply_config`. `crowbar_proto.c` (CDC1) sets
`cfg.engine = CAMPAIGN_ENGINE_CROWBAR`. Otherwise the wire format is
identical — same opcode numbers, same payload layouts.

Operator can confirm which CDC they're talking to via the existing
`PING` opcode on emfi_proto / crowbar_proto, which replies `b"F4"`
for emfi and `b"F5"` for crowbar.

### 4.6 Test coverage

`tests/test_campaign_proto.c` — 17 cases:

- `decode_config`: valid 40-B payload, engine forced by caller,
  wrong length rejected, NULL safety.
- `apply_config`: valid → OK, invalid → REJECTED, NULL → BAD_LEN.
- `serialize_status`: layout, post-tick state reflects DONE,
  too-small buffer = 0.
- `serialize_drain`: empty ring → 1-byte hdr only, 3 results have
  correct field layout, max_count caps at user value, max_count
  caps at buffer space, max_count caps at proto max (18),
  too-small buffer = 0.
- Constants self-check (CMD numbers + payload sizes).

## 5. Reference host client (`tools/campaign_client.py`)

pyserial CLI mirroring `tools/{emfi,crowbar}_client.py` patterns.

Subcommand summary:

| Cmd | Effect |
|---|---|
| `ping --port` | proto round-trip; reply confirms which CDC (b'F4' for EMFI / b'F5' for crowbar) |
| `configure --port --delay START:END:STEP --width X:Y:Z --power A:B:C [--settle MS]` | encode + send 40 B CONFIG |
| `start --port` | CONFIGURING → SWEEPING |
| `stop --port` | halt mid-sweep |
| `status --port` | decode 20 B STATUS reply |
| `drain --port [--max N]` | iterate DRAIN until ring empty |
| `watch --port [--max N] [--every MS]` | poll status + drain in a loop until DONE / STOPPED / ERROR |

Axis syntax: `START:END:STEP` (each int) or `START` (collapses the
axis to one value via step=0). Engine implied by `--port`:
`/dev/ttyACM0` for EMFI, `/dev/ttyACM<crowbar>` for crowbar.

The watch loop has a documented 30 ms gap between STATUS and DRAIN
requests in each iteration. Without it the firmware's executor-wait-
loop + pump-driven dispatch occasionally orders the two replies in
a way the host's read window misses the drain bytes. 30 ms is well
below a useful poll period and removes the race in practice;
long-term cleanup belongs in a future async refactor of the
executor.

## 6. Mutual exclusion contract — full picture (F8 + F9)

Five potential consumers compete for the scanner-header pins:

```
drivers/scanner_io          ← passive bulk-read, no claim
services/swd_core           ← F6 — owns SWCLK/SWDIO/(nRST) when inited
services/jtag_core          ← F8-1 — owns TDI/TDO/TMS/TCK/(TRST)
services/pinout_scanner     ← F8-2 — claims via swd_phy / jtag_core
services/buspirate_compat   ← F8-4 — uses jtag_core
services/flashrom_serprog   ← F8-5 — claims CS/MOSI/MISO/SCK directly
services/campaign_manager   ← F9-3 — verify hook only; bus released
                              between fires
services/daplink_usb        ← F7 — pending; will use swd_phy + the
                              F9 mutex
```

Three layers of exclusion:

1. **Hardware-pin claim** (drivers / phys): `swd_phy_init` /
   `jtag_init` claim PIO instance + GPIOs. Refuse if already
   claimed elsewhere.
2. **Shell soft-lock** (F8-1): `swd init` ↔ `jtag init` reject each
   other. Catches operator-driven concurrency.
3. **Service mutex** (F9-1): `swd_bus_lock` covers campaign / scanner
   / daplink contention. F8-2's scanner adopts the mutex in
   F-future; F7's daplink_usb adopts it on landing.

## 7. Physical smoke results (2026-04-28, FaultyCat v2.2)

Per `apps/faultycat_fw/faultycat.uf2` from `rewrite/v3` HEAD:

| Check | Result |
|---|---|
| USB enumeration (1209:fa17, 4×CDC) | ✓ |
| `campaign demo crowbar` shell command (F9-3 path) | ✓ 6/6 streamed |
| `campaign_client.py configure → start → watch` (F9-4/F9-5 path, 6 steps) | ✓ 6/6 streamed |
| `campaign_client.py` 8-step variant | ✓ 8/8 streamed |
| Mutex acquired/released per step (no deadlock) | ✓ |
| Result ringbuffer drain via host_proto binary | ✓ |
| F4 EMFI ping regression | ✓ `PONG F4` |
| F5 crowbar ping regression | ✓ `PONG F5` |
| F8-1 jtag init/chain/idcode (no target) | ✓ chain=0, ERR no_target |
| F8-2 `scan jtag` (clean bus) | ✓ NO_MATCH |
| F8-4 BusPirate handshake (BBIO1 + OCD1) | ✓ |
| F3 magic-baud BOOTSEL regression | ✓ |
| Re-flash post-BOOTSEL | ✓ |

**Not verified physically** (need external targets):
- Real SWD verify hook (waits on F6 HW unblock).
- Glitch campaign against an actual target (would need an SPI flash
  / nRF52 / etc. wired through the scanner header AND the SWD path
  unblocked).
- F7 daplink_usb integration with the mutex (waits on F7).

## 8. Future work (post-F9)

- **F7 — CMSIS-DAP daplink_usb**: parser for the Vendor IF + HID v1
  bulk endpoints. Adopts `swd_bus_try_acquire(DAPLINK)` and replies
  `DAP_ERROR(busy)` on contention per plan §4. Blocked on F6 SWD
  HW gate.
- **Real SWD verify hook**: F-future replaces
  `campaign_dispatch_executor`'s no-op verify with `swd_dp_read32`
  against a baseline address (PC, configurable register, target-
  specific). Returns the read value into `result.target_state` and
  flags divergence in `verify_status`.
- **Async executor**: split `campaign_executor_*` so it doesn't park
  dispatch against itself during the engine wait loop. Cleans up
  the 30 ms gap currently needed in `campaign_client.py watch`.
- **Scanner adopts the mutex**: `pinout_scan_jtag/swd` calls
  `swd_bus_acquire(SCANNER)` per candidate so a campaign-vs-scanner
  race (today only reachable by clever scripting) is well-defined.
- **F10 — faultycmd-rs**: replaces `tools/{emfi,crowbar,campaign}_
  client.py` with a Rust workspace + ratatui TUI dashboard.

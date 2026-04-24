# F4 — EMFI glitch engine service (design spec)

Date: 2026-04-24
Branch: `rewrite/v3`
Tag at close: `v3.0-f4`
Entry state: `v3.0-f3` (USB composite up, 16/16 endpoints, diag on CDC2,
DAP stub passing `DAP_Info`, 75 host tests green).

## 1. Goal

Deliver the **first real service** on top of drivers + HAL: the EMFI
engine. It orchestrates `drivers/hv_charger`, `drivers/emfi_pulse`,
`drivers/ext_trigger`, and `drivers/target_monitor` to execute a
configured fire sequence — wait for external trigger, PIO-timed delay,
PIO-timed HV pulse on GP14, ADC capture around the pulse — and expose
it over a binary protocol on CDC0.

Exit criteria:

- Pulse on GP14 with sub-µs jitter and ±1 µs delay accuracy on a
  physical scope. CPU-timed button fire (F2b path) keeps working.
- A host client (minimal Python reference) runs
  `configure → arm → fire → status` over CDC0 `/dev/ttyACM0`.
- F3 invariants intact: `lsusb -v` shows 10 interfaces, DAP_Info
  replies on vendor IF, CDC2 scanner diag prints snapshot.
- Host test suite grows by PIO/DMA fakes + state-machine tests and
  stays green. No existing test regresses.

## 2. Scope / non-scope

In scope:

- `hal/pio.h` lift (F4-1): thin RP2040 PIO wrapper, board-agnostic.
- `hal/dma.h` lift (F4-2): channel claim, ring config, DREQ source.
- `services/glitch_engine/emfi/` (F4-3 → F4-5): PIO program, ADC
  capture, campaign state machine + API.
- `services/host_proto/emfi_proto/` (F4-6): binary framing over CDC0.
- Integration into `apps/faultycat_fw/main.c`: CDC0 RX → proto parser
  → service. Button PULSE keeps calling `emfi_pulse_fire_manual`.
- New safety-gate invariant: `emfi_pulse` won't PIO-fire unless
  `hv_charger_is_charged()` was true within the last 100 ms (wire up
  the F2b-announced gate in SAFETY.md §3 item 5).

Out of scope:

- Pre-trigger ADC ring (scope-style capture with history). F9.
- Serial/UART pattern trigger. F5 (crowbar) and F9 (campaign) will
  need it; designing it once there keeps this spec tight.
- Campaign sweeps (delay/width scans with verification). F9.
- Host tool Rust workspace. F10. F4 ships a minimal Python reference
  client in `tools/emfi_client.py` only.

## 3. Sub-commit plan (one tag at close)

Same shape as F2a/F2b and F3-1..F3-4. Each row is one commit. HV-
signed ones carry the full SAFETY.md §1 block in the commit body.

| Sub | Scope | SIGNED |
|-----|-------|--------|
| F4-1 | `hal/pio.h` + `hal/src/rp2040/pio.c` + fakes `tests/hal_fake/pio.c` + Unity cases for SM/program/FIFO/IRQ. No board code. | No |
| F4-2 | `hal/dma.h` + `hal/src/rp2040/dma.c` + fakes + Unity cases for channel claim, ring, DREQ. No board code. | No |
| F4-3 | `services/glitch_engine/emfi/emfi_pio.{h,c}` — compiled PIO program driving GP14 (trigger wait + delay + pulse). `drivers/emfi_pulse` gains `_attach_pio`/`_detach_pio`. Host tests: program builder + fake SM. | **Yes** (first commit wiring PIO into EMFI path + `emfi_pulse` change) |
| F4-4 | `services/glitch_engine/emfi/emfi_capture.{h,c}` — ADC DMA ring on GP29 (8 KB), start/stop around fire window. Host tests: fake DMA, ring config, buffer contract. | No |
| F4-5 | `services/glitch_engine/emfi/emfi_campaign.{h,c}` — `configure/arm/fire/status` API + state machine. Orchestrates hv_charger, emfi_pulse, emfi_pio, emfi_capture, ext_trigger. Activates the HV-within-100ms invariant. | **Yes** |
| F4-6 | `services/host_proto/emfi_proto/` — TLV+CRC framing + command handlers. `main.c` integration on CDC0. Docs live-update (ARCHITECTURE status snapshot + PORTING rows 6, 9, 10). Annotated tag `v3.0-f4`. | **Yes** (top-level wiring) |

## 4. `hal/pio.h` API (F4-1)

Thin wrapper — HAL stays free of FaultyCat knowledge.

```c
// hal/pio.h
typedef struct hal_pio_inst hal_pio_inst_t;   // opaque, 1 per PIO block
typedef struct {
    hal_pio_inst_t *pio;
    uint32_t        sm;          // 0..3
    uint32_t        offset;      // program load offset
    bool            claimed;
} hal_pio_sm_t;

typedef struct {
    const uint16_t *instructions;
    uint32_t        length;
    int32_t         origin;      // -1 = anywhere
} hal_pio_program_t;

hal_pio_inst_t *hal_pio_instance(uint8_t which);   // 0 or 1
bool  hal_pio_can_add_program(hal_pio_inst_t *, const hal_pio_program_t *);
bool  hal_pio_add_program(hal_pio_inst_t *, const hal_pio_program_t *, uint32_t *offset_out);
void  hal_pio_remove_program(hal_pio_inst_t *, const hal_pio_program_t *, uint32_t offset);
void  hal_pio_clear_instruction_memory(hal_pio_inst_t *);

bool  hal_pio_claim_sm(hal_pio_inst_t *, uint32_t sm);
void  hal_pio_unclaim_sm(hal_pio_inst_t *, uint32_t sm);
void  hal_pio_sm_set_enabled(hal_pio_inst_t *, uint32_t sm, bool);
void  hal_pio_sm_clear_fifos(hal_pio_inst_t *, uint32_t sm);
void  hal_pio_sm_restart(hal_pio_inst_t *, uint32_t sm);

void  hal_pio_sm_put_blocking(hal_pio_inst_t *, uint32_t sm, uint32_t word);
bool  hal_pio_sm_try_put(hal_pio_inst_t *, uint32_t sm, uint32_t word);
bool  hal_pio_sm_try_get(hal_pio_inst_t *, uint32_t sm, uint32_t *out);

bool  hal_pio_irq_get(hal_pio_inst_t *, uint32_t irq_index);
void  hal_pio_irq_clear(hal_pio_inst_t *, uint32_t irq_index);

// Minimal config helpers — enough to hand an SM a program + pin set
// + clock divisor. Full config surface stays behind the hal/.
typedef struct {
    uint32_t set_pin_base;
    uint32_t set_pin_count;
    uint32_t sideset_pin_base;   // 0 if unused
    uint32_t sideset_pin_count;  // 0 if unused
    uint32_t in_pin_base;        // 0 if unused
    float    clk_div;            // 1.0 == sysclock
} hal_pio_sm_cfg_t;

void  hal_pio_sm_configure(hal_pio_inst_t *, uint32_t sm, uint32_t offset,
                           const hal_pio_sm_cfg_t *);

// GPIO → PIO binding
void  hal_pio_gpio_init(hal_pio_inst_t *, uint32_t gpio);
void  hal_pio_set_consecutive_pindirs(hal_pio_inst_t *, uint32_t sm,
                                      uint32_t base, uint32_t count, bool is_out);
```

Fakes (host tests): deterministic in-memory FIFOs per SM, a bit-vector
for IRQ lines, counters for `put_blocking`/`try_get`, and a record of
loaded programs. No PIO simulation — we test callers, not the silicon.

**PIO instance allocation convention** (documented in
ARCHITECTURE.md at F4-1 close):

- `pio0` reserved for EMFI (F4) and crowbar (F5).
- `pio1` reserved for debugprobe-derived SWD (F6), target UART
  passthrough (F8), JTAG (F8). 4 SMs split across these is tight
  but workable; validated in their own phases.

## 5. `hal/dma.h` API (F4-2)

Also thin. RP2040 has 12 channels; no need for a pool abstraction yet.

```c
// hal/dma.h
typedef int hal_dma_channel_t;   // -1 == invalid

typedef enum {
    HAL_DMA_SIZE_8  = 0,
    HAL_DMA_SIZE_16 = 1,
    HAL_DMA_SIZE_32 = 2,
} hal_dma_xfer_size_t;

typedef enum {
    HAL_DMA_DREQ_ADC    = 0x24,   // DREQ_ADC on RP2040
    HAL_DMA_DREQ_PIO0_0 = 0x00,   // extended later as we need them
    HAL_DMA_DREQ_PIO0_1 = 0x01,
    HAL_DMA_DREQ_FORCE  = 0x3f,   // permanent ready
} hal_dma_dreq_t;

typedef struct {
    hal_dma_xfer_size_t size;
    bool                read_increment;
    bool                write_increment;
    // Ring-mode: when non-zero, write/read address wraps every
    // (1 << ring_bits) bytes. Matches RP2040 ring config directly.
    uint32_t            ring_bits;      // 0 = disabled
    bool                ring_on_write;  // true = ring on dst, false = ring on src
    hal_dma_dreq_t      dreq;
} hal_dma_cfg_t;

hal_dma_channel_t hal_dma_claim_unused(void);
void  hal_dma_unclaim(hal_dma_channel_t);
void  hal_dma_configure(hal_dma_channel_t, const hal_dma_cfg_t *,
                        volatile void *dst, const volatile void *src,
                        uint32_t transfer_count, bool start);
void  hal_dma_start(hal_dma_channel_t);
void  hal_dma_abort(hal_dma_channel_t);
bool  hal_dma_is_busy(hal_dma_channel_t);
uint32_t hal_dma_transfer_count(hal_dma_channel_t);
```

Fakes: channel pool of 12 slots, record of configure calls, simulated
`is_busy` toggle via a test-only hook, `transfer_count` driven by the
test. Keep ADC/PIO interaction out of fakes — tests at this layer
verify *our* calls, not silicon timing.

## 6. `services/glitch_engine/emfi/emfi_pio` (F4-3)

### 6.1 Program structure

One linearised PIO program on `pio0` SM 0. Reimplemented from scratch
under BSD-3; no code path copied from `hextreeio/faultier`.

Phases, each pushable/pullable via FIFO:

```
[PULL] delay_ticks        ; word pushed by CPU before arm
[PULL] pulse_width_ticks  ; word pushed by CPU before arm
<trigger block>           ; compiled per TRIGGER_TYPE
<delay block>             ; OSR copied from FIFO, decrement-to-zero
<pulse block>             ; SET pins=1, decrement pulse_width, SET pins=0
IRQ 0                     ; signal GLITCHED back to CPU
```

Trigger block compile table:

| Type | Block |
|------|-------|
| `IMMEDIATE` | empty — falls through into delay immediately |
| `EXT_RISING` | `wait 0 pin 0 ; wait 1 pin 0` |
| `EXT_FALLING` | `wait 1 pin 0 ; wait 0 pin 0` |
| `EXT_PULSE_POS` | `wait 0 pin 0 ; wait 1 pin 0 ; wait 0 pin 0` |

`in_pin_base` bound to GP8 (ext_trigger). `set_pin_base` bound to GP14
(emfi_pulse). Clock divisor picked so one tick ≈ 8 ns (sysclock
125 MHz ÷ 1.0 → 8 ns/cycle, with 2-cycle WAIT/DEC instructions this
yields ~62.5 M delay ticks/sec; 1 µs delay ≈ 62 ticks).

**Tick/µs translation** lives in the service (`emfi_pio_us_to_ticks`),
verified by host tests against a fixed 125 MHz reference. Jitter
budget documented in the closing F4 commit message.

### 6.2 `emfi_pulse` driver additions (F4-3, SIGNED)

```c
// drivers/emfi_pulse.h additions
bool emfi_pulse_attach_pio(hal_pio_inst_t *pio, uint32_t sm);
void emfi_pulse_detach_pio(void);
bool emfi_pulse_is_attached_to_pio(void);
```

Contract:

- While attached, GP14 is owned by PIO (`pio_gpio_init` +
  `set_consecutive_pindirs(..., true)`). `emfi_pulse_fire_manual`
  returns false without firing. `emfi_pulse_force_low` still works
  and detaches implicitly.
- On detach, GP14 returns to plain GPIO output driven LOW. Restores
  all F2b invariants.

### 6.3 emfi_pio API

```c
typedef enum {
    EMFI_TRIG_IMMEDIATE    = 0,
    EMFI_TRIG_EXT_RISING   = 1,
    EMFI_TRIG_EXT_FALLING  = 2,
    EMFI_TRIG_EXT_PULSE_POS= 3,
} emfi_trig_t;

typedef struct {
    emfi_trig_t   trigger;
    uint32_t      delay_us;      // 0..1_000_000
    uint32_t      width_us;      // 1..50 (mirrors EMFI_PULSE_MIN/MAX)
} emfi_pio_params_t;

bool emfi_pio_init(void);        // claims pio0/SM0, loads program stub
void emfi_pio_deinit(void);
bool emfi_pio_load(const emfi_pio_params_t *);   // compiles + loads
bool emfi_pio_start(void);       // pushes delay/width, enables SM
bool emfi_pio_is_done(void);     // polls IRQ_GLITCHED
void emfi_pio_clear_done(void);
uint32_t emfi_pio_ticks_per_us(void);
```

## 7. `services/glitch_engine/emfi/emfi_capture` (F4-4)

8 KB ring on GP29 (ADC channel 3), 8-bit samples, DREQ_ADC. Matches
legacy `glitcher.c::prepare_adc` which is FaultyCat-origin code and
license-clean (BSD-3 under the rewrite).

```c
#define EMFI_CAPTURE_BUFFER_BYTES  8192u

bool emfi_capture_init(void);
void emfi_capture_start(void);            // adc_run(true) + DMA armed
void emfi_capture_stop(void);             // adc_run(false) + DMA abort
const uint8_t *emfi_capture_buffer(void); // points into the ring
uint32_t       emfi_capture_fill(void);   // 0..8192, saturates at 8192
```

Start is idempotent. Implementation claims one DMA channel via
`hal_dma_claim_unused` at init and holds it for service lifetime.

## 8. `services/glitch_engine/emfi/emfi_campaign` (F4-5, SIGNED)

### 8.1 Public API

```c
typedef struct {
    emfi_trig_t   trigger;
    uint32_t      delay_us;
    uint32_t      width_us;
    uint32_t      charge_timeout_ms;  // 0 = wait up to hv_charger auto-disarm (60s)
} emfi_config_t;

typedef enum {
    EMFI_STATE_IDLE        = 0,
    EMFI_STATE_ARMING      = 1,
    EMFI_STATE_CHARGED     = 2,
    EMFI_STATE_WAITING     = 3,  // trigger wait
    EMFI_STATE_FIRED       = 4,
    EMFI_STATE_ERROR       = 5,
} emfi_state_t;

typedef enum {
    EMFI_ERR_NONE              = 0,
    EMFI_ERR_BAD_CONFIG        = 1,
    EMFI_ERR_HV_NOT_CHARGED    = 2,  // charge_timeout exhausted
    EMFI_ERR_TRIGGER_TIMEOUT   = 3,  // set at GO level by host
    EMFI_ERR_PIO_FAULT         = 4,
    EMFI_ERR_INTERNAL          = 5,
} emfi_err_t;

typedef struct {
    emfi_state_t  state;
    emfi_err_t    err;
    uint32_t      last_fire_at_ms;
    uint32_t      capture_fill;
    uint32_t      pulse_width_us_actual;
    uint32_t      delay_us_actual;
} emfi_status_t;

bool emfi_campaign_init(void);
bool emfi_campaign_configure(const emfi_config_t *);
bool emfi_campaign_arm(void);
bool emfi_campaign_fire(uint32_t trigger_timeout_ms);
void emfi_campaign_disarm(void);
void emfi_campaign_tick(void);                 // poll from main loop
void emfi_campaign_get_status(emfi_status_t *);
const uint8_t *emfi_campaign_capture_buffer(void);
uint32_t       emfi_campaign_capture_len(void);
```

### 8.2 State machine

```
IDLE ── configure() ──► IDLE' (config stored)
IDLE ── arm() ────────► ARMING
                         │
                         ├─ hv_charger_arm() + note arm_start_ms
                         │  poll hv_charger_is_charged() each tick
                         │
            tick: charged within timeout? ──┬─ yes ──► CHARGED
                                            └─ no  ──► ERROR(HV_NOT_CHARGED)

CHARGED ── fire(to) ──► WAITING
                         │
                         ├─ note hv_last_charged_ms = now
                         │  (satisfies SAFETY.md §3 item 5 "HV within 100 ms" invariant)
                         │  emfi_pulse_attach_pio(); emfi_pio_load(); emfi_pio_start();
                         │  emfi_capture_start()
                         │
            tick: emfi_pio_is_done()? ──┬─ yes ──► FIRED
                                        └─ elapsed > to? ──► ERROR(TRIGGER_TIMEOUT)

FIRED → emfi_capture_stop(); emfi_pio_deinit(); emfi_pulse_detach_pio();
        hv_charger_disarm(); state → IDLE (with last_* fields populated).

ERROR  → same teardown as FIRED, but state stays ERROR until configure() or disarm() clears it.

Any disarm() from any state: teardown everything, state → IDLE.
```

### 8.3 Safety invariants activated here

1. `emfi_pulse` cannot PIO-fire without `emfi_campaign` observing
   `hv_charger_is_charged() == true` within the last 100 ms. If more
   than 100 ms elapses between CHARGED→WAITING, re-check before
   pushing PIO start. On miss, transition ERROR.
2. Every terminal state (`FIRED`, `ERROR`) MUST end with
   `hv_charger_disarm()` unconditionally.
3. `tick()` MUST be called from main loop; service never busy-waits.

## 9. `services/host_proto/emfi_proto` (F4-6, SIGNED)

### 9.1 Framing

```
 ┌──────┬──────────┬──────────┬────────────┐
 │ SOF  │ CMD (1B) │ LEN (2B) │ PAYLOAD    │  + CRC16 (2B)
 │ 0xFA │          │   LE     │  0..512 B  │    CCITT / poly 0x1021, init 0xFFFF
 └──────┴──────────┴──────────┴────────────┘
```

Host→device commands in F4:

| CMD  | Name       | Payload                                    | Reply |
|------|------------|--------------------------------------------|-------|
| 0x01 | PING       | —                                          | 0x81 PONG, 4 ASCII "F4\0\0" |
| 0x10 | CONFIGURE  | `{trigger:u8, delay_us:u32, width_us:u32, charge_timeout_ms:u32}` | 0x90 ACK (err code) |
| 0x11 | ARM        | —                                          | 0x91 ACK (err code) |
| 0x12 | FIRE       | `{trigger_timeout_ms:u32}`                 | 0x92 ACK (err code) |
| 0x13 | DISARM     | —                                          | 0x93 ACK |
| 0x14 | STATUS     | —                                          | 0x94 STATUS, emfi_status_t packed |
| 0x15 | CAPTURE    | `{offset:u16, len:u16}` (len ≤ 512)        | 0x95 CAPTURE, raw bytes |

CRC computed over CMD + LEN + PAYLOAD. Replies share the same framing
(SOF + CMD | 0x80 + LEN + PAYLOAD + CRC). Malformed frame → discard,
reset parser state. No framing escape; LEN + CRC carry ambiguity.

### 9.2 Parser

State machine (`WAIT_SOF → CMD → LEN_LO → LEN_HI → PAYLOAD → CRC_LO → CRC_HI`).
Rejects LEN > 512. Timeout of 100 ms between bytes → reset to
`WAIT_SOF`. Single static buffer per CDC (only CDC0 for now).

### 9.3 Integration in `main.c`

- Initialize `emfi_campaign_init` after `usb_composite_init`.
- Per-loop: `emfi_campaign_tick()`; `emfi_proto_pump()` reads CDC0
  available bytes and drives the parser.
- CDC2 diag behaviour unchanged. Snapshot line gains one column
  `EMFI=<state>` so operators can see state transitions live.
- Button PULSE path unchanged: still calls `emfi_pulse_fire_manual`
  unless `emfi_pulse_is_attached_to_pio() == true`, in which case
  the snapshot prints `PULSE button: PIO-attached, ignored`.

## 10. Test strategy

### 10.1 Host tests (Unity)

- `test_hal_pio`: program add/remove, SM claim idempotency, FIFO
  put/get, IRQ get/clear, program build matches expected instruction
  stream.
- `test_hal_dma`: channel claim pool exhaustion, ring-bit math,
  configure records correct fields, start/abort state.
- `test_emfi_pio_compile`: given each trigger type, the compiled
  program matches the golden instruction list.
- `test_emfi_capture`: init claims one DMA channel; start arms it;
  stop aborts; fill clamps at 8192; buffer pointer stable.
- `test_emfi_campaign`: state machine — IDLE→ARMING→CHARGED→WAITING
  →FIRED happy path; each error path (HV timeout, trigger timeout,
  PIO fault) maps to the expected `emfi_err_t`; disarm from every
  state ends IDLE; SAFETY 100 ms invariant asserted by simulated
  clock advance.
- `test_emfi_proto`: frame parser fuzzing (random byte streams →
  parser never crashes, only valid frames deliver); CRC correctness;
  LEN overflow rejection; inter-byte timeout.

### 10.2 Hardware checkpoint (Sabas, physical)

Performed after the F4-6 commit, before the `v3.0-f4` tag:

1. `lsusb -v` still shows 10 interfaces. `openocd … DAP_Info` still replies.
2. `tools/emfi_client.py --fire --trigger immediate --delay 0 --width 5` on a board with coil mounted + shield on → scope on SMA shows a ~5 µs pulse.
3. `--trigger ext-rising --delay 100 --width 5` with a 1 kHz square wave on GP8 → pulse appears 100 µs after each rising edge, jitter < 100 ns, delay within ±1 µs of set point (averaged 1000 shots).
4. Charge timeout exercised: `--charge-timeout 100` with HV disconnected → ERROR(HV_NOT_CHARGED) returned, HV auto-disarmed.
5. Button PULSE still fires CPU-timed pulse when service is IDLE.
6. Button PULSE ignored (with diag log) when service is WAITING.
7. 75 existing host tests + new tests all green.

## 11. Risks

1. **PIO SM budget on pio0.** If the single-SM linearised program
   doesn't fit 32 instructions including the largest trigger block,
   split across 2 SMs (trigger SM → IRQ → delay+pulse SM). Decided
   in F4-3 once instruction count is measured. ARCHITECTURE.md
   updated accordingly.
2. **Tick scheduling.** `emfi_campaign_tick` called once per main
   loop iteration (currently every 20 ms via `BUTTON_POLL_PERIOD_MS`).
   Charge detection latency up to 20 ms is acceptable for the 60 s
   auto-disarm. If it isn't, main loop gains a `hal_sleep_us(1000)`
   path during ARMING/WAITING — isolated change.
3. **SAFETY 100 ms invariant false-positive.** If charging is
   marginal and CHARGED debounces around the 100 ms window, the
   service reports ERROR(HV_NOT_CHARGED) mid-fire. Mitigation:
   10 ms resample hysteresis in `emfi_campaign_tick` before the
   invariant check. Document in SAFETY.md if the mitigation lands.
4. **Host client debt.** `tools/emfi_client.py` is a thin pyserial
   script, NOT the F10 Rust client. It stays in `tools/` and is not
   a shipped product.

## 12. Docs live-update contract

- **ARCHITECTURE.md** — status snapshot row for F4, big tree gets
  `hal/pio ✓ F4-1`, `hal/dma ✓ F4-2`, `services/glitch_engine/emfi/
  ✓ F4`, and the pio0/pio1 allocation paragraph.
- **PORTING.md** — tick rows 6 (PIO fire), 9 (ft_pio — stays "not
  ported"), 10 (compilers — stays "not ported"); add a new row
  noting `emfi_capture.c` derives from the legacy
  `glitcher.c::prepare_adc` (license-clean).
- **SAFETY.md §3** — update item 5 from "planned for F2b-4" to
  "activated in F4-5 commit <hash>".
- **`.claude/skills/faultycat-fase-actual/SKILL.md`** — close F4,
  open F5 context.

All doc updates land in the F4-6 commit body (plan rule §4 "tests
antes de commit", skill rule "docs live-update", SAFETY §1 signed
checklist applies).

## 13. Out of scope (explicit list)

- Pre-trigger ADC ring / scope-style capture — F9.
- Multi-trigger orchestration (serial pattern, N-shot campaign
  sweep) — F9.
- SWD verification after glitch — F9 (needs SWD core from F6).
- Rust faultycmd — F10.
- CMSIS-DAP real engine — F7.
- Any hw_board changes — out of scope forever.

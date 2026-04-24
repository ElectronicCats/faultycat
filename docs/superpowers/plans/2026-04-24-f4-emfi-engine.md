# F4 — EMFI glitch engine service implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the first real service on FaultyCat v3 — a PIO-timed EMFI
glitch engine with ADC capture, orchestrated behind a binary protocol on
CDC0, without regressing F3's USB composite.

**Architecture:** 6 sequential sub-commits (F4-1..F4-6, one tag `v3.0-f4`
at close). F4-1/F4-2 lift `hal/pio` + `hal/dma`. F4-3/F4-4 build the
EMFI PIO program + ADC ring. F4-5 orchestrates state machine + SAFETY
100ms invariant. F4-6 wires binary protocol and `main.c`. Three commits
are HV-signed (F4-3/F4-5/F4-6).

**Tech Stack:** C11, pico-sdk 2.1.1, Unity host tests with HAL fakes,
RP2040 PIO assembler (hand-authored instructions), RP2040 DMA ring mode,
TinyUSB CDC.

**Spec reference:** `docs/superpowers/specs/2026-04-24-f4-emfi-engine-design.md`.
Do not relitigate decisions made there.

---

## Pre-flight checks (before any F4 commit)

- [ ] **Step 0: Verify we are on the right branch and tree is clean**

```bash
git rev-parse --abbrev-ref HEAD    # expect: rewrite/v3
git status --porcelain              # expect: empty (except pre-existing Hardware/FaultyCat.kicad_pro)
git describe --tags --abbrev=0      # expect: v3.0-f3
```

- [ ] **Step 0.1: Confirm baseline tests are green**

```bash
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
```

Expected: all 75 tests across 10 binaries pass.

- [ ] **Step 0.2: Confirm firmware still builds**

```bash
cmake --preset fw-debug
cmake --build --preset fw-debug
```

Expected: `build/fw-debug/apps/faultycat_fw/faultycat.uf2` produced, no errors.

---

## File structure (the whole F4 surface)

**Created in F4-1:**
- `hal/include/hal/pio.h`
- `hal/src/rp2040/pio.c`
- `tests/hal_fake/hal_fake_pio.h`
- `tests/hal_fake/pio_fake.c`
- `tests/test_hal_pio.c`

**Created in F4-2:**
- `hal/include/hal/dma.h`
- `hal/src/rp2040/dma.c`
- `tests/hal_fake/hal_fake_dma.h`
- `tests/hal_fake/dma_fake.c`
- `tests/test_hal_dma.c`

**Created in F4-3 (SIGNED):**
- `services/CMakeLists.txt`
- `services/glitch_engine/CMakeLists.txt`
- `services/glitch_engine/emfi/CMakeLists.txt`
- `services/glitch_engine/emfi/emfi_pio.h`
- `services/glitch_engine/emfi/emfi_pio.c`
- `tests/test_emfi_pio.c`

**Modified in F4-3 (SIGNED):**
- `drivers/emfi_pulse/emfi_pulse.h` (+ `_attach_pio`, `_detach_pio`, `_is_attached_to_pio`)
- `drivers/emfi_pulse/emfi_pulse.c`
- `drivers/emfi_pulse/CMakeLists.txt` (depend on hal_rp2040's new pio target)
- `tests/test_emfi_pulse.c` (add attach/detach cases)

**Created in F4-4:**
- `services/glitch_engine/emfi/emfi_capture.h`
- `services/glitch_engine/emfi/emfi_capture.c`
- `tests/test_emfi_capture.c`
- `tests/hal_fake/hal_fake_adc.h` (extend with ADC FIFO/DREQ hooks)
- `tests/hal_fake/adc_fake.c` (extend)

**Created in F4-5 (SIGNED):**
- `services/glitch_engine/emfi/emfi_campaign.h`
- `services/glitch_engine/emfi/emfi_campaign.c`
- `tests/test_emfi_campaign.c`

**Modified in F4-5 (SIGNED):**
- `docs/SAFETY.md` (activate §3 invariant 5)

**Created in F4-6 (SIGNED):**
- `services/host_proto/CMakeLists.txt`
- `services/host_proto/emfi_proto/CMakeLists.txt`
- `services/host_proto/emfi_proto/emfi_proto.h`
- `services/host_proto/emfi_proto/emfi_proto.c`
- `tests/test_emfi_proto.c`
- `tools/emfi_client.py`

**Modified in F4-6 (SIGNED):**
- `apps/faultycat_fw/main.c` (service init + tick + CDC0 pump)
- `apps/faultycat_fw/CMakeLists.txt`
- `CMakeLists.txt` (firmware branch: `add_subdirectory(services)`)
- `tests/CMakeLists.txt` (register 5 new tests)
- `docs/ARCHITECTURE.md` (status snapshot + tree + pio0/pio1 paragraph)
- `docs/PORTING.md` (rows 6, 9, 10 + new row for emfi_capture)
- `.claude/skills/faultycat-fase-actual/SKILL.md` (close F4, open F5)

---

## Commit message templates

Unsigned commits (F4-1, F4-2, F4-4) use a plain feat/footer:

```
feat(F4-N): <subject>

<body>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

SIGNED commits (F4-3, F4-5, F4-6) use the full SAFETY.md §1 block in
the body. Template:

```
feat(F4-N): <subject>

<body>

Safety: HV charger is in DISARMED state at firmware boot. [y]
Safety: 60-second auto-disarm is active by default and tested. [y]
Safety: plastic shield is installed for any physical test of this change. [y/NA]
Safety: operator has a known-good GND reference. [y/NA]
Safety: the output SMA is either loaded by an EMFI coil or discharged. [y/NA]
Safety: oscilloscope probe is rated for the measured voltage (10x / 100x / differential). [y/NA]
Safety: break-before-make on MOSFET gate transitions is preserved by this change. [NA]
Safety-justification: this commit only modifies the EMFI path; crowbar gate logic is unchanged.
Safety: build, flash, and verify were performed in person by the maintainer. [y/NA]
Signed-off-by: Sabas <sabasjimenez@gmail.com>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

Sabas fills in y/n/NA and signs at commit time — the agent must stop
and ask for the filled-in block before running the commit for any SIGNED
row.

---

## F4-1 — Lift `hal/pio` (UNSIGNED)

### Task 1.1: Write `hal/include/hal/pio.h`

**Files:**
- Create: `hal/include/hal/pio.h`

- [ ] **Step 1: Replace the `#error` stub with the full API**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// HAL — PIO
//
// Portable access to the RP2040 PIO block: state machines, programs,
// FIFOs, IRQs, and the minimal pin-binding surface callers need.
//
// Scope: everything the EMFI trigger/delay/pulse compiler and the
// crowbar glitcher need. SWD/UART/JTAG PIO work in later phases will
// extend this header as new primitives are demanded.

typedef struct hal_pio_inst hal_pio_inst_t;  // opaque, one per PIO block

typedef struct {
    const uint16_t *instructions;  // raw PIO instructions
    uint32_t        length;        // instruction count
    int32_t         origin;        // -1 = anywhere, else forced offset
} hal_pio_program_t;

typedef struct {
    uint32_t set_pin_base;
    uint32_t set_pin_count;       // 0 = no SET pins bound
    uint32_t sideset_pin_base;
    uint32_t sideset_pin_count;   // 0 = no sideset; >0 wires sm_config_set_sideset
    bool     sideset_optional;    // only used when sideset_pin_count > 0
    bool     sideset_pindirs;     // only used when sideset_pin_count > 0
    uint32_t in_pin_base;         // valid only when in_pin_count > 0 (0 is a real pin)
    uint32_t in_pin_count;        // 0 = no IN pins bound
    float    clk_div;             // 1.0 == sysclock
} hal_pio_sm_cfg_t;

// Obtain a handle to PIO block `which` (0 or 1). Returns NULL if the
// argument is out of range.
hal_pio_inst_t *hal_pio_instance(uint8_t which);

// True if `program` can be loaded into `pio` at this moment.
bool hal_pio_can_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *program);

// Load `program`. Writes the chosen offset into `*offset_out` on
// success. Returns false if no room.
bool hal_pio_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *program,
                        uint32_t *offset_out);

// Remove a previously-loaded program at `offset`.
void hal_pio_remove_program(hal_pio_inst_t *pio, const hal_pio_program_t *program,
                           uint32_t offset);

// Wipe all loaded instructions. Use sparingly — invalidates every
// offset returned by add_program.
void hal_pio_clear_instruction_memory(hal_pio_inst_t *pio);

// Claim state machine `sm` (0..3). Returns false if already claimed.
bool hal_pio_claim_sm(hal_pio_inst_t *pio, uint32_t sm);
void hal_pio_unclaim_sm(hal_pio_inst_t *pio, uint32_t sm);

// Apply `cfg` and the loaded program offset to state machine `sm`.
// Leaves the SM disabled; caller enables explicitly.
void hal_pio_sm_configure(hal_pio_inst_t *pio, uint32_t sm, uint32_t offset,
                         const hal_pio_sm_cfg_t *cfg);

void hal_pio_sm_set_enabled(hal_pio_inst_t *pio, uint32_t sm, bool enabled);
void hal_pio_sm_clear_fifos(hal_pio_inst_t *pio, uint32_t sm);
void hal_pio_sm_restart(hal_pio_inst_t *pio, uint32_t sm);

// Blocks until the TX FIFO can accept `word`.
void hal_pio_sm_put_blocking(hal_pio_inst_t *pio, uint32_t sm, uint32_t word);
// Non-blocking put; returns false if TX FIFO full.
bool hal_pio_sm_try_put(hal_pio_inst_t *pio, uint32_t sm, uint32_t word);
// Non-blocking get; returns false if RX FIFO empty.
bool hal_pio_sm_try_get(hal_pio_inst_t *pio, uint32_t sm, uint32_t *out);

// Read / clear one of the 8 PIO IRQ lines (0..7). The PIO program
// raises these with IRQ instructions; CPU polls from the main loop.
bool hal_pio_irq_get(hal_pio_inst_t *pio, uint32_t irq_index);
void hal_pio_irq_clear(hal_pio_inst_t *pio, uint32_t irq_index);

// Route GPIO `gpio` to PIO control. Must precede any program output
// onto that pin.
void hal_pio_gpio_init(hal_pio_inst_t *pio, uint32_t gpio);

// Set direction of `count` consecutive pins starting at `base`. When
// is_out is true, the SM drives them as outputs; false leaves them
// floating inputs.
void hal_pio_set_consecutive_pindirs(hal_pio_inst_t *pio, uint32_t sm,
                                    uint32_t base, uint32_t count, bool is_out);
```

- [ ] **Step 2: Stage the file**

```bash
git add hal/include/hal/pio.h
```

### Task 1.2: Write `tests/hal_fake/hal_fake_pio.h`

**Files:**
- Create: `tests/hal_fake/hal_fake_pio.h`

- [ ] **Step 1: Define inspection state**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/pio.h"

#define HAL_FAKE_PIO_INSTANCES    2
#define HAL_FAKE_PIO_SM_PER_INST  4
#define HAL_FAKE_PIO_IRQ_COUNT    8
#define HAL_FAKE_PIO_FIFO_DEPTH   16
#define HAL_FAKE_PIO_PROGRAM_MAX  32

typedef struct {
    bool      claimed;
    bool      enabled;
    uint32_t  configured_offset;
    hal_pio_sm_cfg_t last_cfg;
    uint32_t  configure_calls;
    uint32_t  enable_calls;
    uint32_t  clear_fifo_calls;
    uint32_t  restart_calls;
    uint32_t  tx_fifo[HAL_FAKE_PIO_FIFO_DEPTH];
    uint32_t  tx_count;
    uint32_t  rx_fifo[HAL_FAKE_PIO_FIFO_DEPTH];
    uint32_t  rx_count;
    uint32_t  pindirs_calls;
    uint32_t  last_pindirs_base;
    uint32_t  last_pindirs_count;
    bool      last_pindirs_is_out;
} hal_fake_pio_sm_state_t;

typedef struct {
    uint16_t  instructions[HAL_FAKE_PIO_PROGRAM_MAX];
    uint32_t  length;
    uint32_t  base_offset;
    bool      loaded;
} hal_fake_pio_program_slot_t;

typedef struct {
    hal_fake_pio_sm_state_t     sm[HAL_FAKE_PIO_SM_PER_INST];
    hal_fake_pio_program_slot_t program;   // single-slot model is enough for F4
    bool                         irq[HAL_FAKE_PIO_IRQ_COUNT];
    uint32_t                     gpio_init_bitmap;  // bit N = gpio_init called for pin N
    uint32_t                     clear_memory_calls;
} hal_fake_pio_inst_state_t;

extern hal_fake_pio_inst_state_t hal_fake_pio_insts[HAL_FAKE_PIO_INSTANCES];

void hal_fake_pio_reset(void);

// Test-only hook: push `word` onto SM `sm`'s RX FIFO so try_get can
// return it. Used by tests that simulate PIO → CPU traffic.
void hal_fake_pio_push_rx(uint8_t which, uint32_t sm, uint32_t word);

// Test-only hook: raise IRQ `irq_index` so hal_pio_irq_get returns true.
void hal_fake_pio_raise_irq(uint8_t which, uint32_t irq_index);
```

- [ ] **Step 2: Stage**

```bash
git add tests/hal_fake/hal_fake_pio.h
```

### Task 1.3: Write the fake implementation

**Files:**
- Create: `tests/hal_fake/pio_fake.c`

- [ ] **Step 1: Implement fake**

```c
#include "hal_fake_pio.h"

#include <string.h>

hal_fake_pio_inst_state_t hal_fake_pio_insts[HAL_FAKE_PIO_INSTANCES];

void hal_fake_pio_reset(void) {
    memset(hal_fake_pio_insts, 0, sizeof(hal_fake_pio_insts));
}

// The "instance" returned from hal_pio_instance is actually a pointer
// into the fake state array; callers treat it opaquely so the cast
// does not leak.
hal_pio_inst_t *hal_pio_instance(uint8_t which) {
    if (which >= HAL_FAKE_PIO_INSTANCES) return NULL;
    return (hal_pio_inst_t *)&hal_fake_pio_insts[which];
}

static hal_fake_pio_inst_state_t *as_state(hal_pio_inst_t *pio) {
    return (hal_fake_pio_inst_state_t *)pio;
}

bool hal_pio_can_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p) {
    if (!pio || !p) return false;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    if (s->program.loaded) return false;
    return p->length <= HAL_FAKE_PIO_PROGRAM_MAX;
}

bool hal_pio_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                        uint32_t *offset_out) {
    if (!hal_pio_can_add_program(pio, p)) return false;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    memcpy(s->program.instructions, p->instructions,
           p->length * sizeof(uint16_t));
    s->program.length      = p->length;
    s->program.base_offset = (p->origin >= 0) ? (uint32_t)p->origin : 0u;
    s->program.loaded      = true;
    if (offset_out) *offset_out = s->program.base_offset;
    return true;
}

void hal_pio_remove_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                           uint32_t offset) {
    (void)p; (void)offset;
    if (!pio) return;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    memset(&s->program, 0, sizeof(s->program));
}

void hal_pio_clear_instruction_memory(hal_pio_inst_t *pio) {
    if (!pio) return;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    memset(&s->program, 0, sizeof(s->program));
    s->clear_memory_calls++;
}

bool hal_pio_claim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return false;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    if (s->sm[sm].claimed) return false;
    s->sm[sm].claimed = true;
    return true;
}

void hal_pio_unclaim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    as_state(pio)->sm[sm].claimed = false;
}

void hal_pio_sm_configure(hal_pio_inst_t *pio, uint32_t sm, uint32_t offset,
                         const hal_pio_sm_cfg_t *cfg) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST || !cfg) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->configured_offset = offset;
    smst->last_cfg          = *cfg;
    smst->configure_calls++;
}

void hal_pio_sm_set_enabled(hal_pio_inst_t *pio, uint32_t sm, bool en) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->enabled = en;
    smst->enable_calls++;
}

void hal_pio_sm_clear_fifos(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->tx_count = 0;
    smst->rx_count = 0;
    smst->clear_fifo_calls++;
}

void hal_pio_sm_restart(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    as_state(pio)->sm[sm].restart_calls++;
}

void hal_pio_sm_put_blocking(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    if (smst->tx_count < HAL_FAKE_PIO_FIFO_DEPTH) {
        smst->tx_fifo[smst->tx_count++] = word;
    }
}

bool hal_pio_sm_try_put(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return false;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    if (smst->tx_count >= HAL_FAKE_PIO_FIFO_DEPTH) return false;
    smst->tx_fifo[smst->tx_count++] = word;
    return true;
}

bool hal_pio_sm_try_get(hal_pio_inst_t *pio, uint32_t sm, uint32_t *out) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST || !out) return false;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    if (smst->rx_count == 0) return false;
    *out = smst->rx_fifo[0];
    for (uint32_t i = 1; i < smst->rx_count; i++) {
        smst->rx_fifo[i - 1] = smst->rx_fifo[i];
    }
    smst->rx_count--;
    return true;
}

bool hal_pio_irq_get(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index >= HAL_FAKE_PIO_IRQ_COUNT) return false;
    return as_state(pio)->irq[irq_index];
}

void hal_pio_irq_clear(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index >= HAL_FAKE_PIO_IRQ_COUNT) return;
    as_state(pio)->irq[irq_index] = false;
}

void hal_pio_gpio_init(hal_pio_inst_t *pio, uint32_t gpio) {
    // Cap is fake-internal: (1u << 32) is UB in C, and the bitmap is
    // 32 bits wide. Real impl forwards any value to pico-sdk (asserts
    // on invalid). Tests should only pass gpio ∈ [0, 29] (RP2040 range).
    if (!pio || gpio >= 32) return;
    as_state(pio)->gpio_init_bitmap |= (1u << gpio);
}

void hal_pio_set_consecutive_pindirs(hal_pio_inst_t *pio, uint32_t sm,
                                    uint32_t base, uint32_t count, bool is_out) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->pindirs_calls++;
    smst->last_pindirs_base   = base;
    smst->last_pindirs_count  = count;
    smst->last_pindirs_is_out = is_out;
}

// Test-only hooks
void hal_fake_pio_push_rx(uint8_t which, uint32_t sm, uint32_t word) {
    if (which >= HAL_FAKE_PIO_INSTANCES || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &hal_fake_pio_insts[which].sm[sm];
    if (smst->rx_count < HAL_FAKE_PIO_FIFO_DEPTH) {
        smst->rx_fifo[smst->rx_count++] = word;
    }
}

void hal_fake_pio_raise_irq(uint8_t which, uint32_t irq_index) {
    if (which >= HAL_FAKE_PIO_INSTANCES || irq_index >= HAL_FAKE_PIO_IRQ_COUNT) return;
    hal_fake_pio_insts[which].irq[irq_index] = true;
}
```

- [ ] **Step 2: Register fake in `tests/hal_fake/CMakeLists.txt`**

Edit `tests/hal_fake/CMakeLists.txt`, add `pio_fake.c` to the
`add_library(hal_fake STATIC ...)` list so the fourth entry becomes:

```cmake
add_library(hal_fake STATIC
    gpio_fake.c
    time_fake.c
    adc_fake.c
    pwm_fake.c
    pio_fake.c
)
```

- [ ] **Step 3: Stage**

```bash
git add tests/hal_fake/pio_fake.c tests/hal_fake/CMakeLists.txt
```

### Task 1.4: Write Unity tests for `hal/pio`

**Files:**
- Create: `tests/test_hal_pio.c`

- [ ] **Step 1: Write the tests**

```c
// Unit tests for hal/pio — exercised against the host fake.

#include "unity.h"

#include "hal/pio.h"
#include "hal_fake_pio.h"

void setUp(void) { hal_fake_pio_reset(); }
void tearDown(void) {}

static void test_instance_returns_distinct_handles(void) {
    hal_pio_inst_t *a = hal_pio_instance(0);
    hal_pio_inst_t *b = hal_pio_instance(1);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NULL(hal_pio_instance(2));
}

static void test_add_program_returns_offset_and_marks_loaded(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    uint16_t prog[] = { 0xE000, 0xE001, 0xE002 };
    hal_pio_program_t p = { .instructions = prog, .length = 3, .origin = -1 };
    TEST_ASSERT_TRUE(hal_pio_can_add_program(pio, &p));
    uint32_t offset = 0xFFFFFFFF;
    TEST_ASSERT_TRUE(hal_pio_add_program(pio, &p, &offset));
    TEST_ASSERT_EQUAL_UINT32(0u, offset);
    TEST_ASSERT_FALSE(hal_pio_can_add_program(pio, &p));  // slot full
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].program.loaded);
    TEST_ASSERT_EQUAL_UINT32(3u, hal_fake_pio_insts[0].program.length);
}

static void test_remove_program_frees_slot(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    uint16_t prog[] = { 0xE000 };
    hal_pio_program_t p = { .instructions = prog, .length = 1, .origin = -1 };
    uint32_t off = 0;
    hal_pio_add_program(pio, &p, &off);
    hal_pio_remove_program(pio, &p, off);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].program.loaded);
    TEST_ASSERT_TRUE(hal_pio_can_add_program(pio, &p));
}

static void test_clear_instruction_memory_counts_and_frees(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_clear_instruction_memory(pio);
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[0].clear_memory_calls);
}

static void test_claim_sm_is_exclusive(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    TEST_ASSERT_TRUE(hal_pio_claim_sm(pio, 0));
    TEST_ASSERT_FALSE(hal_pio_claim_sm(pio, 0));
    hal_pio_unclaim_sm(pio, 0);
    TEST_ASSERT_TRUE(hal_pio_claim_sm(pio, 0));
    // Out-of-range sm rejected.
    TEST_ASSERT_FALSE(hal_pio_claim_sm(pio, 4));
}

static void test_configure_records_offset_and_cfg(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_cfg_t cfg = {
        .set_pin_base = 14, .set_pin_count = 1,
        .sideset_pin_base = 0, .sideset_pin_count = 0,
        .in_pin_base = 8, .in_pin_count = 1, .clk_div = 1.0f,
    };
    hal_pio_sm_configure(pio, 2, 5u, &cfg);
    TEST_ASSERT_EQUAL_UINT32(5u, hal_fake_pio_insts[0].sm[2].configured_offset);
    TEST_ASSERT_EQUAL_UINT32(14u, hal_fake_pio_insts[0].sm[2].last_cfg.set_pin_base);
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[0].sm[2].configure_calls);
}

static void test_sm_enable_tracks_state(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_set_enabled(pio, 1, true);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[1].enabled);
    hal_pio_sm_set_enabled(pio, 1, false);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[1].enabled);
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[1].enable_calls);
}

static void test_fifo_put_and_get_round_trip(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_put_blocking(pio, 0, 0xDEADBEEF);
    hal_pio_sm_put_blocking(pio, 0, 0xCAFEBABE);
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[0].tx_count);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, hal_fake_pio_insts[0].sm[0].tx_fifo[0]);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABE, hal_fake_pio_insts[0].sm[0].tx_fifo[1]);

    // push_rx simulates PIO → CPU and try_get drains in FIFO order.
    hal_fake_pio_push_rx(0, 0, 0x11);
    hal_fake_pio_push_rx(0, 0, 0x22);
    uint32_t got = 0;
    TEST_ASSERT_TRUE(hal_pio_sm_try_get(pio, 0, &got));
    TEST_ASSERT_EQUAL_HEX32(0x11, got);
    TEST_ASSERT_TRUE(hal_pio_sm_try_get(pio, 0, &got));
    TEST_ASSERT_EQUAL_HEX32(0x22, got);
    TEST_ASSERT_FALSE(hal_pio_sm_try_get(pio, 0, &got));
}

static void test_clear_fifos_drops_both_sides(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_put_blocking(pio, 3, 0xAA);
    hal_fake_pio_push_rx(0, 3, 0xBB);
    hal_pio_sm_clear_fifos(pio, 3);
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_pio_insts[0].sm[3].tx_count);
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_pio_insts[0].sm[3].rx_count);
}

static void test_irq_raise_get_clear(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    TEST_ASSERT_FALSE(hal_pio_irq_get(pio, 0));
    hal_fake_pio_raise_irq(0, 0);
    TEST_ASSERT_TRUE(hal_pio_irq_get(pio, 0));
    hal_pio_irq_clear(pio, 0);
    TEST_ASSERT_FALSE(hal_pio_irq_get(pio, 0));
}

static void test_gpio_init_sets_bitmap(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_gpio_init(pio, 14);
    hal_pio_gpio_init(pio, 8);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].gpio_init_bitmap & (1u << 14));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].gpio_init_bitmap & (1u << 8));
}

static void test_sm_restart_counts_calls(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_restart(pio, 2);
    hal_pio_sm_restart(pio, 2);
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[2].restart_calls);
}

static void test_set_consecutive_pindirs_records_args(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_set_consecutive_pindirs(pio, 0, 14, 1, true);
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[0].sm[0].pindirs_calls);
    TEST_ASSERT_EQUAL_UINT32(14u, hal_fake_pio_insts[0].sm[0].last_pindirs_base);
    TEST_ASSERT_EQUAL_UINT32(1u,  hal_fake_pio_insts[0].sm[0].last_pindirs_count);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[0].last_pindirs_is_out);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_instance_returns_distinct_handles);
    RUN_TEST(test_add_program_returns_offset_and_marks_loaded);
    RUN_TEST(test_remove_program_frees_slot);
    RUN_TEST(test_clear_instruction_memory_counts_and_frees);
    RUN_TEST(test_claim_sm_is_exclusive);
    RUN_TEST(test_configure_records_offset_and_cfg);
    RUN_TEST(test_sm_enable_tracks_state);
    RUN_TEST(test_fifo_put_and_get_round_trip);
    RUN_TEST(test_clear_fifos_drops_both_sides);
    RUN_TEST(test_irq_raise_get_clear);
    RUN_TEST(test_gpio_init_sets_bitmap);
    RUN_TEST(test_sm_restart_counts_calls);
    RUN_TEST(test_set_consecutive_pindirs_records_args);
    return UNITY_END();
}
```

- [ ] **Step 2: Register the test** — append to `tests/CMakeLists.txt` after `faultycat_add_test(test_time)`:

```cmake
# F4-1 — hal/pio unit tests
faultycat_add_test(test_hal_pio)
```

- [ ] **Step 3: Stage**

```bash
git add tests/test_hal_pio.c tests/CMakeLists.txt
```

### Task 1.5: Run host tests — expect the F4-1 tests to pass

- [ ] **Step 1: Build + run**

```bash
cmake --preset host-tests
cmake --build --preset host-tests
ctest --preset host-tests
```

Expected: all previous tests pass + `test_hal_pio` passes (11 new cases).
Totals should be 76 tests across 11 binaries. If any test fails, fix
before continuing.

### Task 1.6: Write real RP2040 implementation

**Files:**
- Create: `hal/src/rp2040/pio.c`

- [ ] **Step 1: Write impl**

```c
#include "hal/pio.h"

#include "hardware/pio.h"
#include "hardware/gpio.h"

// hal_pio_inst_t is really a pico-sdk PIO. Cast on the way in.
static inline PIO as_pio(hal_pio_inst_t *p) { return (PIO)p; }

hal_pio_inst_t *hal_pio_instance(uint8_t which) {
    if (which == 0) return (hal_pio_inst_t *)pio0;
    if (which == 1) return (hal_pio_inst_t *)pio1;
    return NULL;
}

bool hal_pio_can_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p) {
    if (!pio || !p) return false;
    pio_program_t prog = { .instructions = p->instructions,
                           .length       = (uint8_t)p->length,
                           .origin       = (int8_t)p->origin };
    return pio_can_add_program(as_pio(pio), &prog);
}

bool hal_pio_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                        uint32_t *offset_out) {
    if (!pio || !p) return false;
    pio_program_t prog = { .instructions = p->instructions,
                           .length       = (uint8_t)p->length,
                           .origin       = (int8_t)p->origin };
    int off = pio_add_program(as_pio(pio), &prog);
    if (off < 0) return false;
    if (offset_out) *offset_out = (uint32_t)off;
    return true;
}

void hal_pio_remove_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                           uint32_t offset) {
    if (!pio || !p) return;
    pio_program_t prog = { .instructions = p->instructions,
                           .length       = (uint8_t)p->length,
                           .origin       = (int8_t)p->origin };
    pio_remove_program(as_pio(pio), &prog, (uint)offset);
}

void hal_pio_clear_instruction_memory(hal_pio_inst_t *pio) {
    if (!pio) return;
    pio_clear_instruction_memory(as_pio(pio));
}

bool hal_pio_claim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return false;
    if (pio_sm_is_claimed(as_pio(pio), (uint)sm)) return false;
    pio_sm_claim(as_pio(pio), (uint)sm);
    return true;
}

void hal_pio_unclaim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return;
    pio_sm_unclaim(as_pio(pio), (uint)sm);
}

void hal_pio_sm_configure(hal_pio_inst_t *pio, uint32_t sm, uint32_t offset,
                         const hal_pio_sm_cfg_t *cfg) {
    if (!pio || sm > 3 || !cfg) return;
    pio_sm_config c = pio_get_default_sm_config();
    if (cfg->set_pin_count) {
        sm_config_set_set_pins(&c, cfg->set_pin_base, cfg->set_pin_count);
    }
    if (cfg->sideset_pin_count) {
        sm_config_set_sideset(&c, cfg->sideset_pin_count,
                              cfg->sideset_optional, cfg->sideset_pindirs);
        sm_config_set_sideset_pins(&c, cfg->sideset_pin_base);
    }
    if (cfg->in_pin_count) {
        sm_config_set_in_pins(&c, cfg->in_pin_base);
    }
    sm_config_set_clkdiv(&c, cfg->clk_div <= 0.0f ? 1.0f : cfg->clk_div);
    pio_sm_init(as_pio(pio), (uint)sm, (uint)offset, &c);
}

void hal_pio_sm_set_enabled(hal_pio_inst_t *pio, uint32_t sm, bool en) {
    if (!pio || sm > 3) return;
    pio_sm_set_enabled(as_pio(pio), (uint)sm, en);
}

void hal_pio_sm_clear_fifos(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return;
    pio_sm_clear_fifos(as_pio(pio), (uint)sm);
}

void hal_pio_sm_restart(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return;
    pio_sm_restart(as_pio(pio), (uint)sm);
}

void hal_pio_sm_put_blocking(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm > 3) return;
    pio_sm_put_blocking(as_pio(pio), (uint)sm, word);
}

bool hal_pio_sm_try_put(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm > 3) return false;
    if (pio_sm_is_tx_fifo_full(as_pio(pio), (uint)sm)) return false;
    pio_sm_put(as_pio(pio), (uint)sm, word);
    return true;
}

bool hal_pio_sm_try_get(hal_pio_inst_t *pio, uint32_t sm, uint32_t *out) {
    if (!pio || sm > 3 || !out) return false;
    if (pio_sm_is_rx_fifo_empty(as_pio(pio), (uint)sm)) return false;
    *out = pio_sm_get(as_pio(pio), (uint)sm);
    return true;
}

bool hal_pio_irq_get(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index > 7) return false;
    return pio_interrupt_get(as_pio(pio), (uint)irq_index);
}

void hal_pio_irq_clear(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index > 7) return;
    pio_interrupt_clear(as_pio(pio), (uint)irq_index);
}

void hal_pio_gpio_init(hal_pio_inst_t *pio, uint32_t gpio) {
    if (!pio) return;
    pio_gpio_init(as_pio(pio), (uint)gpio);
}

void hal_pio_set_consecutive_pindirs(hal_pio_inst_t *pio, uint32_t sm,
                                    uint32_t base, uint32_t count, bool is_out) {
    if (!pio || sm > 3) return;
    pio_sm_set_consecutive_pindirs(as_pio(pio), (uint)sm, (uint)base,
                                   (uint)count, is_out);
}
```

- [ ] **Step 2: Wire into `hal/CMakeLists.txt`**

Edit `hal/CMakeLists.txt`:

```cmake
add_library(hal_rp2040 STATIC
    src/rp2040/gpio.c
    src/rp2040/time.c
    src/rp2040/adc.c
    src/rp2040/pwm.c
    src/rp2040/pio.c
)

target_link_libraries(hal_rp2040
    PUBLIC
        pico_stdlib
        hardware_gpio
        hardware_adc
        hardware_pwm
        hardware_clocks
        hardware_pio
)
```

- [ ] **Step 3: Stage**

```bash
git add hal/src/rp2040/pio.c hal/CMakeLists.txt
```

### Task 1.7: Verify firmware still builds

- [ ] **Step 1: Build firmware preset**

```bash
cmake --preset fw-debug
cmake --build --preset fw-debug
```

Expected: `build/fw-debug/apps/faultycat_fw/faultycat.uf2` produced, no errors.

### Task 1.8: Commit F4-1

- [ ] **Step 1: Verify everything staged**

```bash
git status
```

Expected staged: `hal/include/hal/pio.h`, `hal/src/rp2040/pio.c`,
`hal/CMakeLists.txt`, `tests/hal_fake/hal_fake_pio.h`,
`tests/hal_fake/pio_fake.c`, `tests/hal_fake/CMakeLists.txt`,
`tests/test_hal_pio.c`, `tests/CMakeLists.txt`.

- [ ] **Step 2: Commit (UNSIGNED — does not touch HV paths)**

```bash
git commit -m "$(cat <<'EOF'
feat(F4-1): lift hal/pio — SM/program/FIFO/IRQ + host fake + 11 Unity cases

RP2040 PIO wrapper behind hal/ so future services (EMFI in F4-3,
crowbar in F5, SWD in F6, JTAG/UART in F8) stop taking pico-sdk
hardware/pio.h directly.

Scope today: program add/remove/clear, SM claim/configure/enable,
FIFO put/try_get, IRQ get/clear, GPIO→PIO binding, pindirs. pio.h
contract is small on purpose — extended when later phases demand
a primitive.

Host fake records every call for test inspection; push_rx and
raise_irq let tests simulate PIO→CPU traffic without simulating
the assembler. 11 Unity cases. Totals now 76 tests / 11 binaries.

PIO instance allocation convention (documented in ARCHITECTURE.md
at F4-6 close): pio0 → EMFI (F4) + crowbar (F5); pio1 → SWD (F6)
+ target UART (F8) + JTAG (F8).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 3: Verify tree green**

```bash
ctest --preset host-tests
cmake --build --preset fw-debug
```

Expected: ctest all green, fw-debug builds.

---

## F4-2 — Lift `hal/dma` (UNSIGNED)

### Task 2.1: Write `hal/include/hal/dma.h`

**Files:**
- Create: `hal/include/hal/dma.h`

- [ ] **Step 1: Replace the `#error` stub**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

// HAL — DMA
//
// Portable access to the RP2040 DMA controller. Minimal surface for
// the ADC capture ring used by services/glitch_engine/emfi/ and
// (later) services/glitch_engine/crowbar/. The RP2040 has 12 channels;
// claim_unused walks the pool, nothing else is abstracted.

typedef int hal_dma_channel_t;   // -1 == invalid

typedef enum {
    HAL_DMA_SIZE_8  = 0,
    HAL_DMA_SIZE_16 = 1,
    HAL_DMA_SIZE_32 = 2,
} hal_dma_xfer_size_t;

typedef enum {
    HAL_DMA_DREQ_PIO0_0 = 0x00,
    HAL_DMA_DREQ_PIO0_1 = 0x01,
    HAL_DMA_DREQ_PIO0_2 = 0x02,
    HAL_DMA_DREQ_PIO0_3 = 0x03,
    HAL_DMA_DREQ_PIO1_0 = 0x08,
    HAL_DMA_DREQ_PIO1_1 = 0x09,
    HAL_DMA_DREQ_PIO1_2 = 0x0A,
    HAL_DMA_DREQ_PIO1_3 = 0x0B,
    HAL_DMA_DREQ_ADC    = 0x24,
    HAL_DMA_DREQ_FORCE  = 0x3F,
} hal_dma_dreq_t;

typedef struct {
    hal_dma_xfer_size_t size;
    bool                read_increment;
    bool                write_increment;
    // Ring-mode: when non-zero, write/read wraps every
    // (1 << ring_bits) bytes. 0 disables ring mode.
    uint32_t            ring_bits;
    bool                ring_on_write;   // true = ring dst, false = ring src
    hal_dma_dreq_t      dreq;
} hal_dma_cfg_t;

// Claim the first unused channel. Returns -1 if all 12 are busy.
hal_dma_channel_t hal_dma_claim_unused(void);
void hal_dma_unclaim(hal_dma_channel_t ch);

// Program the channel. If `start` is true the transfer begins
// immediately; otherwise call hal_dma_start explicitly.
void hal_dma_configure(hal_dma_channel_t ch, const hal_dma_cfg_t *cfg,
                       volatile void *dst, const volatile void *src,
                       uint32_t transfer_count, bool start);

void hal_dma_start(hal_dma_channel_t ch);
void hal_dma_abort(hal_dma_channel_t ch);
bool hal_dma_is_busy(hal_dma_channel_t ch);
uint32_t hal_dma_transfer_count(hal_dma_channel_t ch);
```

- [ ] **Step 2: Stage**

```bash
git add hal/include/hal/dma.h
```

### Task 2.2: Write `tests/hal_fake/hal_fake_dma.h`

**Files:**
- Create: `tests/hal_fake/hal_fake_dma.h`

- [ ] **Step 1: Define state**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/dma.h"

#define HAL_FAKE_DMA_CHANNELS 12

typedef struct {
    bool           claimed;
    bool           busy;
    hal_dma_cfg_t  cfg;
    volatile void *dst;
    const volatile void *src;
    uint32_t       transfer_count;
    uint32_t       configure_calls;
    uint32_t       start_calls;
    uint32_t       abort_calls;
} hal_fake_dma_state_t;

extern hal_fake_dma_state_t hal_fake_dma_channels[HAL_FAKE_DMA_CHANNELS];

void hal_fake_dma_reset(void);

// Test-only hooks to let tests poke is_busy and transfer_count.
void hal_fake_dma_set_busy(hal_dma_channel_t ch, bool busy);
void hal_fake_dma_set_transfer_count(hal_dma_channel_t ch, uint32_t n);
```

- [ ] **Step 2: Stage**

```bash
git add tests/hal_fake/hal_fake_dma.h
```

### Task 2.3: Write fake impl

**Files:**
- Create: `tests/hal_fake/dma_fake.c`

- [ ] **Step 1: Write**

```c
#include "hal_fake_dma.h"

#include <string.h>

hal_fake_dma_state_t hal_fake_dma_channels[HAL_FAKE_DMA_CHANNELS];

void hal_fake_dma_reset(void) {
    memset(hal_fake_dma_channels, 0, sizeof(hal_fake_dma_channels));
}

hal_dma_channel_t hal_dma_claim_unused(void) {
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (!hal_fake_dma_channels[i].claimed) {
            hal_fake_dma_channels[i].claimed = true;
            return i;
        }
    }
    return -1;
}

void hal_dma_unclaim(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].claimed = false;
    hal_fake_dma_channels[ch].busy    = false;
}

void hal_dma_configure(hal_dma_channel_t ch, const hal_dma_cfg_t *cfg,
                       volatile void *dst, const volatile void *src,
                       uint32_t transfer_count, bool start) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS || !cfg) return;
    hal_fake_dma_state_t *s = &hal_fake_dma_channels[ch];
    s->cfg            = *cfg;
    s->dst            = dst;
    s->src            = src;
    s->transfer_count = transfer_count;
    s->configure_calls++;
    if (start) {
        s->busy = true;
        s->start_calls++;
    }
}

void hal_dma_start(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].busy = true;
    hal_fake_dma_channels[ch].start_calls++;
}

void hal_dma_abort(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].busy = false;
    hal_fake_dma_channels[ch].abort_calls++;
}

bool hal_dma_is_busy(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return false;
    return hal_fake_dma_channels[ch].busy;
}

uint32_t hal_dma_transfer_count(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return 0;
    return hal_fake_dma_channels[ch].transfer_count;
}

void hal_fake_dma_set_busy(hal_dma_channel_t ch, bool busy) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].busy = busy;
}

void hal_fake_dma_set_transfer_count(hal_dma_channel_t ch, uint32_t n) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].transfer_count = n;
}
```

- [ ] **Step 2: Register in `tests/hal_fake/CMakeLists.txt`** — append `dma_fake.c` to the list (now 6 entries).

- [ ] **Step 3: Stage**

```bash
git add tests/hal_fake/dma_fake.c tests/hal_fake/CMakeLists.txt
```

### Task 2.4: Write Unity tests

**Files:**
- Create: `tests/test_hal_dma.c`

- [ ] **Step 1: Write tests**

```c
// Unit tests for hal/dma — exercised against the host fake.

#include "unity.h"

#include "hal/dma.h"
#include "hal_fake_dma.h"

void setUp(void) { hal_fake_dma_reset(); }
void tearDown(void) {}

static void test_claim_unused_returns_sequential_channels(void) {
    TEST_ASSERT_EQUAL_INT(0, hal_dma_claim_unused());
    TEST_ASSERT_EQUAL_INT(1, hal_dma_claim_unused());
    TEST_ASSERT_EQUAL_INT(2, hal_dma_claim_unused());
}

static void test_claim_unused_exhausts_pool(void) {
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        TEST_ASSERT_EQUAL_INT(i, hal_dma_claim_unused());
    }
    TEST_ASSERT_EQUAL_INT(-1, hal_dma_claim_unused());
}

static void test_unclaim_returns_channel_to_pool(void) {
    int a = hal_dma_claim_unused();
    int b = hal_dma_claim_unused();
    hal_dma_unclaim(a);
    // next claim reuses the freed slot
    TEST_ASSERT_EQUAL_INT(a, hal_dma_claim_unused());
    (void)b;
}

static void test_configure_records_fields(void) {
    int ch = hal_dma_claim_unused();
    hal_dma_cfg_t cfg = {
        .size = HAL_DMA_SIZE_8,
        .read_increment  = false,
        .write_increment = true,
        .ring_bits       = 13,          // 8192 bytes
        .ring_on_write   = true,
        .dreq            = HAL_DMA_DREQ_ADC,
    };
    static uint8_t buf[8192];
    hal_dma_configure(ch, &cfg, buf, (void *)0xDEADBEEF, 0xFFFFFFFFu, true);
    TEST_ASSERT_EQUAL_UINT(HAL_DMA_SIZE_8, hal_fake_dma_channels[ch].cfg.size);
    TEST_ASSERT_EQUAL_UINT32(13u, hal_fake_dma_channels[ch].cfg.ring_bits);
    TEST_ASSERT_TRUE(hal_fake_dma_channels[ch].cfg.ring_on_write);
    TEST_ASSERT_EQUAL_UINT(HAL_DMA_DREQ_ADC, hal_fake_dma_channels[ch].cfg.dreq);
    TEST_ASSERT_EQUAL_PTR(buf, (void *)hal_fake_dma_channels[ch].dst);
    TEST_ASSERT_TRUE(hal_dma_is_busy(ch));
}

static void test_configure_without_start_does_not_arm(void) {
    int ch = hal_dma_claim_unused();
    hal_dma_cfg_t cfg = { .size = HAL_DMA_SIZE_8, .dreq = HAL_DMA_DREQ_ADC };
    hal_dma_configure(ch, &cfg, (void *)1, (void *)2, 8u, false);
    TEST_ASSERT_FALSE(hal_dma_is_busy(ch));
    hal_dma_start(ch);
    TEST_ASSERT_TRUE(hal_dma_is_busy(ch));
}

static void test_abort_stops_transfer(void) {
    int ch = hal_dma_claim_unused();
    hal_dma_cfg_t cfg = { .size = HAL_DMA_SIZE_8 };
    hal_dma_configure(ch, &cfg, (void *)1, (void *)2, 8u, true);
    TEST_ASSERT_TRUE(hal_dma_is_busy(ch));
    hal_dma_abort(ch);
    TEST_ASSERT_FALSE(hal_dma_is_busy(ch));
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_dma_channels[ch].abort_calls);
}

static void test_transfer_count_reports_set_value(void) {
    int ch = hal_dma_claim_unused();
    hal_fake_dma_set_transfer_count(ch, 1234u);
    TEST_ASSERT_EQUAL_UINT32(1234u, hal_dma_transfer_count(ch));
}

static void test_out_of_range_is_a_noop(void) {
    TEST_ASSERT_FALSE(hal_dma_is_busy(-1));
    TEST_ASSERT_FALSE(hal_dma_is_busy(HAL_FAKE_DMA_CHANNELS));
    hal_dma_unclaim(-1);   // must not crash
    hal_dma_abort(-1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_claim_unused_returns_sequential_channels);
    RUN_TEST(test_claim_unused_exhausts_pool);
    RUN_TEST(test_unclaim_returns_channel_to_pool);
    RUN_TEST(test_configure_records_fields);
    RUN_TEST(test_configure_without_start_does_not_arm);
    RUN_TEST(test_abort_stops_transfer);
    RUN_TEST(test_transfer_count_reports_set_value);
    RUN_TEST(test_out_of_range_is_a_noop);
    return UNITY_END();
}
```

- [ ] **Step 2: Register in `tests/CMakeLists.txt`** after `faultycat_add_test(test_hal_pio)`:

```cmake
# F4-2 — hal/dma unit tests
faultycat_add_test(test_hal_dma)
```

- [ ] **Step 3: Stage + run**

```bash
git add tests/test_hal_dma.c tests/CMakeLists.txt
ctest --preset host-tests
```

Expected: all green, 12 binaries now, 84 tests total.

### Task 2.5: Write real RP2040 impl

**Files:**
- Create: `hal/src/rp2040/dma.c`

- [ ] **Step 1: Write**

```c
#include "hal/dma.h"

#include "hardware/dma.h"

static inline uint to_size(hal_dma_xfer_size_t s) {
    switch (s) {
        case HAL_DMA_SIZE_8:  return DMA_SIZE_8;
        case HAL_DMA_SIZE_16: return DMA_SIZE_16;
        case HAL_DMA_SIZE_32: return DMA_SIZE_32;
    }
    return DMA_SIZE_8;
}

hal_dma_channel_t hal_dma_claim_unused(void) {
    int ch = dma_claim_unused_channel(false);
    return ch;  // pico-sdk returns -1 on fail when required=false
}

void hal_dma_unclaim(hal_dma_channel_t ch) {
    if (ch < 0) return;
    dma_channel_unclaim((uint)ch);
}

void hal_dma_configure(hal_dma_channel_t ch, const hal_dma_cfg_t *cfg,
                       volatile void *dst, const volatile void *src,
                       uint32_t transfer_count, bool start) {
    if (ch < 0 || !cfg) return;
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, to_size(cfg->size));
    channel_config_set_read_increment(&c, cfg->read_increment);
    channel_config_set_write_increment(&c, cfg->write_increment);
    if (cfg->ring_bits) {
        channel_config_set_ring(&c, cfg->ring_on_write, (uint)cfg->ring_bits);
    }
    channel_config_set_dreq(&c, (uint)cfg->dreq);
    dma_channel_configure((uint)ch, &c, dst, src, transfer_count, start);
}

void hal_dma_start(hal_dma_channel_t ch) {
    if (ch < 0) return;
    dma_channel_start((uint)ch);
}

void hal_dma_abort(hal_dma_channel_t ch) {
    if (ch < 0) return;
    dma_channel_abort((uint)ch);
}

bool hal_dma_is_busy(hal_dma_channel_t ch) {
    if (ch < 0) return false;
    return dma_channel_is_busy((uint)ch);
}

uint32_t hal_dma_transfer_count(hal_dma_channel_t ch) {
    if (ch < 0) return 0;
    return dma_channel_hw_addr((uint)ch)->transfer_count;
}
```

- [ ] **Step 2: Wire into `hal/CMakeLists.txt`** — add `src/rp2040/dma.c` to the sources list and `hardware_dma` to the link list:

```cmake
add_library(hal_rp2040 STATIC
    src/rp2040/gpio.c
    src/rp2040/time.c
    src/rp2040/adc.c
    src/rp2040/pwm.c
    src/rp2040/pio.c
    src/rp2040/dma.c
)

target_link_libraries(hal_rp2040
    PUBLIC
        pico_stdlib
        hardware_gpio
        hardware_adc
        hardware_pwm
        hardware_clocks
        hardware_pio
        hardware_dma
)
```

- [ ] **Step 3: Build firmware**

```bash
cmake --build --preset fw-debug
```

Expected: UF2 produced, no errors.

### Task 2.6: Commit F4-2

- [ ] **Step 1: Stage + commit**

```bash
git add hal/src/rp2040/dma.c hal/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(F4-2): lift hal/dma — claim/configure/ring/DREQ + 8 Unity cases

Portable RP2040 DMA controller surface for the ADC capture ring
arriving in F4-4. Kept tight: 12-channel pool, ring-mode config,
DREQ source, start/abort, busy poll, transfer_count read.

Fake substitutes the pico-sdk dma_channel_* layer, tracks configure
fields so tests can assert ring_bits=13 + ring_on_write=true (ie. the
8 KB ring that emfi_capture will use). DREQ_ADC = 0x24 matches the
RP2040 datasheet. Totals now 84 tests / 12 binaries.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 2: Verify green**

```bash
ctest --preset host-tests
cmake --build --preset fw-debug
```

Expected: 84/84 green, UF2 builds.

---

## F4-3 — `emfi_pio` service + driver attach (SIGNED)

Everything in this sub-phase lands in **one signed commit**. Sabas
must approve the filled-in checklist block before the commit runs.

### Task 3.1: Extend `drivers/emfi_pulse` with PIO attach/detach

**Files:**
- Modify: `drivers/emfi_pulse/emfi_pulse.h`
- Modify: `drivers/emfi_pulse/emfi_pulse.c`

- [ ] **Step 1: Extend the header**

Insert at the end of `drivers/emfi_pulse/emfi_pulse.h`, after the
existing `emfi_pulse_fire_manual` declaration:

```c
// Forward decl — the opaque HAL PIO handle.
typedef struct hal_pio_inst hal_pio_inst_t;

// Hand GP14 over to PIO. While attached, emfi_pulse_fire_manual
// refuses to fire (returns false) so the CPU path and the PIO path
// never contend for the pin. The service layer (F4-3/F4-5) is
// responsible for the pio_gpio_init + pindir setup before this call.
//
// SAFETY: a caller that attaches while the HV charger is armed
// transfers control of the HV pulse MOSFET to the PIO program —
// make sure the PIO program holds GP14 LOW until the trigger/delay
// phase completes. docs/SAFETY.md §3 #5 invariant fires in F4-5.
bool emfi_pulse_attach_pio(hal_pio_inst_t *pio, uint32_t sm);

// Return GP14 to plain-GPIO ownership driven LOW. Safe from any
// state. No-op if not attached.
void emfi_pulse_detach_pio(void);

// True iff GP14 currently belongs to PIO.
bool emfi_pulse_is_attached_to_pio(void);
```

- [ ] **Step 2: Implement in `emfi_pulse.c`**

Replace the file body with:

```c
#include "emfi_pulse.h"

#include <stdbool.h>

#include "board_v2.h"
#include "hal/gpio.h"
#include "hal/pio.h"
#include "hal/time.h"

static bool             s_attached = false;
static hal_pio_inst_t  *s_pio      = NULL;
static uint32_t         s_sm       = 0;

void emfi_pulse_init(void) {
    hal_gpio_init(BOARD_GP_HV_PULSE, HAL_GPIO_DIR_OUT);
    hal_gpio_set_pulls(BOARD_GP_HV_PULSE, false, false);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
    s_attached = false;
    s_pio      = NULL;
    s_sm       = 0;
}

void emfi_pulse_force_low(void) {
    // Force implies "back to CPU-owned LOW". If a PIO was attached it
    // no longer is; F4-5 layer is responsible for also tearing down
    // the SM; this driver only owns the pin.
    if (s_attached) {
        hal_gpio_init(BOARD_GP_HV_PULSE, HAL_GPIO_DIR_OUT);
        s_attached = false;
        s_pio      = NULL;
    }
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
}

bool emfi_pulse_fire_manual(uint32_t width_us) {
    if (s_attached) {
        return false;
    }
    if (width_us < EMFI_PULSE_MIN_WIDTH_US
     || width_us > EMFI_PULSE_MAX_WIDTH_US) {
        return false;
    }
    uint32_t cookie = hal_irq_save_and_disable();
    hal_gpio_put(BOARD_GP_HV_PULSE, true);
    hal_busy_wait_us(width_us);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
    hal_irq_restore(cookie);
    hal_sleep_ms(EMFI_PULSE_COOLDOWN_MS);
    return true;
}

bool emfi_pulse_attach_pio(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || s_attached) {
        return false;
    }
    s_pio      = pio;
    s_sm       = sm;
    s_attached = true;
    return true;
}

void emfi_pulse_detach_pio(void) {
    if (!s_attached) {
        return;
    }
    s_attached = false;
    s_pio      = NULL;
    // Return pin to plain GPIO LOW.
    hal_gpio_init(BOARD_GP_HV_PULSE, HAL_GPIO_DIR_OUT);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
}

bool emfi_pulse_is_attached_to_pio(void) {
    return s_attached;
}
```

- [ ] **Step 3: Drop the old body**

(Overwriting replaces it — the above is the whole new file.)

### Task 3.2: Add driver tests for attach/detach

**Files:**
- Modify: `tests/test_emfi_pulse.c`

- [ ] **Step 1: Append cases**

Add these test functions *before* `int main(void)` in
`tests/test_emfi_pulse.c`:

```c
// -----------------------------------------------------------------------------
// F4-3 — PIO attach/detach interaction with CPU fire path
// -----------------------------------------------------------------------------

#include "hal/pio.h"
#include "hal_fake_pio.h"

static void test_attach_pio_succeeds_when_detached(void) {
    emfi_pulse_init();
    hal_fake_pio_reset();
    hal_pio_inst_t *pio = hal_pio_instance(0);
    TEST_ASSERT_TRUE(emfi_pulse_attach_pio(pio, 0));
    TEST_ASSERT_TRUE(emfi_pulse_is_attached_to_pio());
}

static void test_attach_pio_refuses_when_already_attached(void) {
    emfi_pulse_init();
    hal_fake_pio_reset();
    hal_pio_inst_t *pio = hal_pio_instance(0);
    TEST_ASSERT_TRUE(emfi_pulse_attach_pio(pio, 0));
    TEST_ASSERT_FALSE(emfi_pulse_attach_pio(pio, 1));
}

static void test_fire_manual_rejected_while_attached(void) {
    emfi_pulse_init();
    hal_fake_pio_reset();
    hal_pio_inst_t *pio = hal_pio_instance(0);
    emfi_pulse_attach_pio(pio, 0);
    TEST_ASSERT_FALSE(emfi_pulse_fire_manual(5u));
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_detach_pio_returns_pin_to_gpio_low(void) {
    emfi_pulse_init();
    hal_fake_pio_reset();
    hal_pio_inst_t *pio = hal_pio_instance(0);
    emfi_pulse_attach_pio(pio, 0);
    // Simulate PIO leaving the pin high, like a glitched state.
    hal_fake_gpio_states[BOARD_GP_HV_PULSE].level = true;
    emfi_pulse_detach_pio();
    TEST_ASSERT_FALSE(emfi_pulse_is_attached_to_pio());
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_fire_manual_works_again_after_detach(void) {
    emfi_pulse_init();
    hal_fake_pio_reset();
    hal_pio_inst_t *pio = hal_pio_instance(0);
    emfi_pulse_attach_pio(pio, 0);
    emfi_pulse_detach_pio();
    TEST_ASSERT_TRUE(emfi_pulse_fire_manual(5u));
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_force_low_detaches_if_attached(void) {
    emfi_pulse_init();
    hal_fake_pio_reset();
    hal_pio_inst_t *pio = hal_pio_instance(0);
    emfi_pulse_attach_pio(pio, 0);
    emfi_pulse_force_low();
    TEST_ASSERT_FALSE(emfi_pulse_is_attached_to_pio());
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}
```

And extend `main()` to run them:

```c
    RUN_TEST(test_attach_pio_succeeds_when_detached);
    RUN_TEST(test_attach_pio_refuses_when_already_attached);
    RUN_TEST(test_fire_manual_rejected_while_attached);
    RUN_TEST(test_detach_pio_returns_pin_to_gpio_low);
    RUN_TEST(test_fire_manual_works_again_after_detach);
    RUN_TEST(test_force_low_detaches_if_attached);
```

- [ ] **Step 2: Run tests**

```bash
ctest --preset host-tests
```

Expected: all green, `test_emfi_pulse` now has 16 cases (10 existing + 6 new).

### Task 3.3: Create `services/glitch_engine/emfi/emfi_pio.h`

**Files:**
- Create: `services/glitch_engine/emfi/emfi_pio.h`

- [ ] **Step 1: Write header**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/glitch_engine/emfi/emfi_pio — compiled PIO program that
// drives GP14 (HV_PULSE) after an optional external trigger and a
// configurable delay.
//
// Reimplemented from scratch under BSD-3. The faultier trigger/delay/
// glitch compiler architecture inspired the layered approach; no line
// of code is copied from hextreeio/faultier (unlicensed upstream).
//
// PIO allocation: pio0, SM 0. ARCHITECTURE.md documents the repo-
// wide PIO instance convention (pio0 for glitch engines; pio1 for
// SWD/UART/JTAG).

typedef enum {
    EMFI_TRIG_IMMEDIATE     = 0,
    EMFI_TRIG_EXT_RISING    = 1,
    EMFI_TRIG_EXT_FALLING   = 2,
    EMFI_TRIG_EXT_PULSE_POS = 3,
} emfi_trig_t;

typedef struct {
    emfi_trig_t trigger;
    uint32_t    delay_us;      // 0..1_000_000
    uint32_t    width_us;      // 1..50 (mirrors EMFI_PULSE_MIN/MAX)
} emfi_pio_params_t;

// One-time init. Claims pio0/SM0 and prepares the instance. Returns
// false if the SM is already claimed elsewhere.
bool emfi_pio_init(void);

// Release pio0/SM0 and clear its instruction memory. Safe to call
// repeatedly.
void emfi_pio_deinit(void);

// Compile `params` into a PIO program and load it. On success the SM
// is configured (but NOT enabled) and the delay/width words have been
// pushed to the TX FIFO in the order the program expects.
bool emfi_pio_load(const emfi_pio_params_t *params);

// Enable the SM; after this the program starts executing and will
// eventually raise IRQ 0 when the pulse has fired.
bool emfi_pio_start(void);

// Poll IRQ 0 — true once the program has raised the "glitched" IRQ.
bool emfi_pio_is_done(void);

// Clear IRQ 0 so the next start() sees a fresh line.
void emfi_pio_clear_done(void);

// Convert microseconds to PIO ticks at the configured clock divisor.
// Exposed for tests and for the campaign layer's status reports.
uint32_t emfi_pio_ticks_per_us(void);
```

- [ ] **Step 2: Stage**

```bash
git add services/glitch_engine/emfi/emfi_pio.h
```

### Task 3.4: Write `services/glitch_engine/emfi/emfi_pio.c`

**Files:**
- Create: `services/glitch_engine/emfi/emfi_pio.c`

The PIO program is hand-authored 16-bit opcodes. Reference: RP2040
datasheet §3.4 PIO instruction encoding. We use 5 trivial instruction
kinds:

- `PULL block`        = 0x80A0
- `OUT Y, 32`         = 0x6040 (pull a 32-bit word into Y scratch reg)
- `WAIT 0 PIN 0`      = 0x2020 (wait for input pin 0 low)
- `WAIT 1 PIN 0`      = 0x20A0 (wait for input pin 0 high)
- `SET PINS, 1`       = 0xE001 (drive SET pin high)
- `SET PINS, 0`       = 0xE000 (drive SET pin low)
- `JMP Y-- addr`      = 0x0080 | addr  (decrement Y, jump if non-zero)
- `IRQ 0`             = 0xC000 (raise IRQ 0)

The clock divisor is set so each instruction consumes ~8 ns at sys
125 MHz. Each `JMP Y--` is 1 cycle → one tick per decrement, so
`ticks_per_us ≈ 125`. We expose `emfi_pio_ticks_per_us()` returning
that value so the caller can do `delay_ticks = delay_us * ticks_per_us`.

- [ ] **Step 1: Write file**

```c
#include "emfi_pio.h"

#include "board_v2.h"
#include "emfi_pulse.h"
#include "hal/pio.h"

// ---------------------------------------------------------------------------
// PIO instruction encodings (RP2040 datasheet §3.4)
// ---------------------------------------------------------------------------

#define OP_PULL_BLOCK    0x80A0u
#define OP_OUT_Y_32      0x6040u
#define OP_WAIT_0_PIN0   0x2020u
#define OP_WAIT_1_PIN0   0x20A0u
#define OP_SET_PIN_HIGH  0xE001u
#define OP_SET_PIN_LOW   0xE000u
#define OP_IRQ0          0xC000u
static inline uint16_t op_jmp_y_dec(uint8_t addr) {
    return (uint16_t)(0x0080u | (addr & 0x1Fu));
}

// ---------------------------------------------------------------------------
// Clock — 125 MHz / 1.0 = 125 MHz PIO clock. 1 instr = 8 ns nominal,
// so 1 µs = 125 ticks.
// ---------------------------------------------------------------------------
#define EMFI_PIO_CLK_DIV 1.0f
#define EMFI_PIO_TICKS_PER_US 125u

// ---------------------------------------------------------------------------
// Program layout (up to 13 instructions, always ≤ 32).
//
// [0]    PULL block                  ; pull delay_ticks into OSR
// [1]    OUT Y, 32                   ; Y = delay_ticks
// [2..N] trigger block (0..3 instrs) ; compiled from EMFI_TRIG_*
// [N+1]  JMP Y-- self                ; delay loop
// [N+2]  PULL block                  ; pull pulse_width_ticks
// [N+3]  OUT Y, 32                   ; Y = pulse_width_ticks
// [N+4]  SET pins=1                  ; rising edge of pulse
// [N+5]  JMP Y-- self                ; hold high
// [N+6]  SET pins=0                  ; falling edge
// [N+7]  IRQ 0                       ; signal GLITCHED to CPU
// ---------------------------------------------------------------------------

static uint16_t s_prog[24];
static uint32_t s_prog_len;
static hal_pio_inst_t *s_pio = NULL;
static uint32_t s_sm        = 0;
static uint32_t s_offset    = 0;
static bool     s_claimed   = false;
static bool     s_loaded    = false;

static uint32_t s_delay_ticks;
static uint32_t s_width_ticks;

static uint32_t compile_trigger_block(uint16_t *out, emfi_trig_t t) {
    switch (t) {
        case EMFI_TRIG_IMMEDIATE:
            return 0;
        case EMFI_TRIG_EXT_RISING:
            out[0] = OP_WAIT_0_PIN0;
            out[1] = OP_WAIT_1_PIN0;
            return 2;
        case EMFI_TRIG_EXT_FALLING:
            out[0] = OP_WAIT_1_PIN0;
            out[1] = OP_WAIT_0_PIN0;
            return 2;
        case EMFI_TRIG_EXT_PULSE_POS:
            out[0] = OP_WAIT_0_PIN0;
            out[1] = OP_WAIT_1_PIN0;
            out[2] = OP_WAIT_0_PIN0;
            return 3;
    }
    return 0;
}

static void build_program(const emfi_pio_params_t *p) {
    s_prog_len = 0;
    s_prog[s_prog_len++] = OP_PULL_BLOCK;
    s_prog[s_prog_len++] = OP_OUT_Y_32;
    s_prog_len += compile_trigger_block(&s_prog[s_prog_len], p->trigger);
    uint8_t delay_loop_addr = (uint8_t)s_prog_len;
    s_prog[s_prog_len++] = op_jmp_y_dec(delay_loop_addr);
    s_prog[s_prog_len++] = OP_PULL_BLOCK;
    s_prog[s_prog_len++] = OP_OUT_Y_32;
    s_prog[s_prog_len++] = OP_SET_PIN_HIGH;
    uint8_t hold_loop_addr = (uint8_t)s_prog_len;
    s_prog[s_prog_len++] = op_jmp_y_dec(hold_loop_addr);
    s_prog[s_prog_len++] = OP_SET_PIN_LOW;
    s_prog[s_prog_len++] = OP_IRQ0;
}

bool emfi_pio_init(void) {
    s_pio = hal_pio_instance(0);
    if (!s_pio) return false;
    if (!hal_pio_claim_sm(s_pio, 0)) return false;
    s_sm      = 0;
    s_claimed = true;
    s_loaded  = false;
    return true;
}

void emfi_pio_deinit(void) {
    if (!s_claimed) return;
    if (s_loaded) {
        hal_pio_program_t prog = { .instructions = s_prog,
                                   .length       = s_prog_len,
                                   .origin       = -1 };
        hal_pio_remove_program(s_pio, &prog, s_offset);
        s_loaded = false;
    }
    hal_pio_unclaim_sm(s_pio, s_sm);
    s_claimed = false;
    s_pio     = NULL;
}

bool emfi_pio_load(const emfi_pio_params_t *p) {
    if (!s_claimed || !p) return false;
    if (p->width_us < 1u || p->width_us > 50u) return false;

    build_program(p);
    if (s_loaded) {
        hal_pio_program_t old = { .instructions = s_prog,
                                  .length       = s_prog_len,
                                  .origin       = -1 };
        hal_pio_remove_program(s_pio, &old, s_offset);
        s_loaded = false;
    }

    hal_pio_program_t prog = { .instructions = s_prog,
                               .length       = s_prog_len,
                               .origin       = -1 };
    if (!hal_pio_add_program(s_pio, &prog, &s_offset)) return false;
    s_loaded = true;

    // Attach GP14 to PIO and bind GP8 as in-pin for trigger waits.
    hal_pio_gpio_init(s_pio, BOARD_GP_HV_PULSE);
    hal_pio_set_consecutive_pindirs(s_pio, s_sm, BOARD_GP_HV_PULSE, 1, true);

    if (!emfi_pulse_attach_pio(s_pio, s_sm)) {
        hal_pio_remove_program(s_pio, &prog, s_offset);
        s_loaded = false;
        return false;
    }

    hal_pio_sm_cfg_t cfg = {
        .set_pin_base     = BOARD_GP_HV_PULSE,
        .set_pin_count    = 1,
        .sideset_pin_base = 0,
        .sideset_pin_count= 0,
        .in_pin_base      = BOARD_GP_EXT_TRIGGER,
        .in_pin_count     = 1,
        .clk_div          = EMFI_PIO_CLK_DIV,
    };
    hal_pio_sm_configure(s_pio, s_sm, s_offset, &cfg);
    hal_pio_sm_clear_fifos(s_pio, s_sm);
    hal_pio_irq_clear(s_pio, 0);

    // Cache tick counts; pushed to TX FIFO in emfi_pio_start so the
    // program reads delay first, then width, in that order.
    s_delay_ticks = p->delay_us * EMFI_PIO_TICKS_PER_US;
    s_width_ticks = p->width_us * EMFI_PIO_TICKS_PER_US;
    return true;
}

bool emfi_pio_start(void) {
    if (!s_claimed || !s_loaded) return false;
    hal_pio_sm_put_blocking(s_pio, s_sm, s_delay_ticks);
    hal_pio_sm_put_blocking(s_pio, s_sm, s_width_ticks);
    hal_pio_sm_set_enabled(s_pio, s_sm, true);
    return true;
}

bool emfi_pio_is_done(void) {
    if (!s_claimed) return false;
    return hal_pio_irq_get(s_pio, 0);
}

void emfi_pio_clear_done(void) {
    if (!s_claimed) return;
    hal_pio_irq_clear(s_pio, 0);
    hal_pio_sm_set_enabled(s_pio, s_sm, false);
}

uint32_t emfi_pio_ticks_per_us(void) {
    return EMFI_PIO_TICKS_PER_US;
}
```

- [ ] **Step 2: Stage**

```bash
git add services/glitch_engine/emfi/emfi_pio.c
```

### Task 3.5: Wire `services/` into the firmware build

**Files:**
- Create: `services/CMakeLists.txt`
- Create: `services/glitch_engine/CMakeLists.txt`
- Create: `services/glitch_engine/emfi/CMakeLists.txt`
- Modify: root `CMakeLists.txt`

- [ ] **Step 1: `services/CMakeLists.txt`**

```cmake
# services/ — orchestration layer. Services know multiple drivers
# and implement attack-level features. Built only in firmware mode.

add_subdirectory(glitch_engine)
# host_proto wired in F4-6.
```

- [ ] **Step 2: `services/glitch_engine/CMakeLists.txt`**

```cmake
add_subdirectory(emfi)
# crowbar wired in F5.
```

- [ ] **Step 3: `services/glitch_engine/emfi/CMakeLists.txt`**

```cmake
add_library(service_emfi STATIC
    emfi_pio.c
)

target_include_directories(service_emfi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_emfi
    PUBLIC
        hal_rp2040
        board_v2_header
        driver_emfi_pulse
)
```

- [ ] **Step 4: Root `CMakeLists.txt`** — in the `if(FAULTYCAT_BUILD_FIRMWARE)` block, add `add_subdirectory(services)` right after `add_subdirectory(drivers)`:

```cmake
if(FAULTYCAT_BUILD_FIRMWARE)
    pico_sdk_init()
    add_subdirectory(hal)
    add_subdirectory(drivers)
    add_subdirectory(services)
    add_subdirectory(usb)
    add_subdirectory(apps/faultycat_fw)
endif()
```

- [ ] **Step 5: Stage**

```bash
git add services/CMakeLists.txt services/glitch_engine/CMakeLists.txt services/glitch_engine/emfi/CMakeLists.txt CMakeLists.txt
```

### Task 3.6: Write `tests/test_emfi_pio.c`

**Files:**
- Create: `tests/test_emfi_pio.c`

Because the service links against driver + hal, we need a new helper
in `tests/CMakeLists.txt` that compiles service source + driver
source together. Simplest: reuse `faultycat_add_driver_test` with the
service source added manually.

- [ ] **Step 1: Write tests**

```c
// Unit tests for services/glitch_engine/emfi/emfi_pio — exercised
// against hal fakes + emfi_pulse driver fake.

#include "unity.h"

#include "emfi_pio.h"
#include "emfi_pulse.h"
#include "board_v2.h"
#include "hal/pio.h"
#include "hal_fake_pio.h"
#include "hal_fake_gpio.h"

void setUp(void) {
    hal_fake_pio_reset();
    hal_fake_gpio_reset();
    emfi_pulse_init();
}

void tearDown(void) {
    emfi_pio_deinit();
}

static void test_init_claims_pio0_sm0(void) {
    TEST_ASSERT_TRUE(emfi_pio_init());
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[0].claimed);
}

static void test_init_fails_if_sm0_already_claimed(void) {
    hal_pio_claim_sm(hal_pio_instance(0), 0);
    TEST_ASSERT_FALSE(emfi_pio_init());
}

static void test_deinit_unclaims_and_drops_program(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 10, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    emfi_pio_deinit();
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[0].claimed);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].program.loaded);
}

static void test_load_rejects_zero_width(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 0, .width_us = 0 };
    TEST_ASSERT_FALSE(emfi_pio_load(&p));
}

static void test_load_rejects_width_above_max(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 0, .width_us = 51 };
    TEST_ASSERT_FALSE(emfi_pio_load(&p));
}

static void test_load_immediate_has_no_trigger_block(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 1, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    // Expected program length: 2 (setup delay) + 0 (trigger) + 1 (delay loop)
    // + 2 (setup width) + 1 (SET high) + 1 (hold loop) + 1 (SET low)
    // + 1 (IRQ) = 9.
    TEST_ASSERT_EQUAL_UINT32(9u, hal_fake_pio_insts[0].program.length);
}

static void test_load_rising_edge_inserts_two_waits(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_EXT_RISING,
                           .delay_us = 1, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    // 9 + 2 = 11
    TEST_ASSERT_EQUAL_UINT32(11u, hal_fake_pio_insts[0].program.length);
    // Instructions at offset 2, 3 are WAIT_0, WAIT_1.
    TEST_ASSERT_EQUAL_HEX16(0x2020, hal_fake_pio_insts[0].program.instructions[2]);
    TEST_ASSERT_EQUAL_HEX16(0x20A0, hal_fake_pio_insts[0].program.instructions[3]);
}

static void test_load_pulse_positive_inserts_three_waits(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_EXT_PULSE_POS,
                           .delay_us = 1, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    TEST_ASSERT_EQUAL_UINT32(12u, hal_fake_pio_insts[0].program.length);
}

static void test_load_attaches_emfi_pulse_to_pio(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 1, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    TEST_ASSERT_TRUE(emfi_pulse_is_attached_to_pio());
}

static void test_load_binds_gp14_to_pio(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 1, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].gpio_init_bitmap
                     & (1u << BOARD_GP_HV_PULSE));
}

static void test_load_configures_sm_with_correct_pins(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_EXT_RISING,
                           .delay_us = 1, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    TEST_ASSERT_EQUAL_UINT32(BOARD_GP_HV_PULSE,
                             hal_fake_pio_insts[0].sm[0].last_cfg.set_pin_base);
    TEST_ASSERT_EQUAL_UINT32(BOARD_GP_EXT_TRIGGER,
                             hal_fake_pio_insts[0].sm[0].last_cfg.in_pin_base);
}

static void test_start_pushes_delay_then_width_ticks(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 10, .width_us = 5 };
    emfi_pio_load(&p);
    TEST_ASSERT_TRUE(emfi_pio_start());
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[0].tx_count);
    TEST_ASSERT_EQUAL_UINT32(10u * 125u,
                             hal_fake_pio_insts[0].sm[0].tx_fifo[0]);
    TEST_ASSERT_EQUAL_UINT32(5u * 125u,
                             hal_fake_pio_insts[0].sm[0].tx_fifo[1]);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[0].enabled);
}

static void test_is_done_polls_irq0(void) {
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 1, .width_us = 5 };
    emfi_pio_load(&p);
    emfi_pio_start();
    TEST_ASSERT_FALSE(emfi_pio_is_done());
    hal_fake_pio_raise_irq(0, 0);
    TEST_ASSERT_TRUE(emfi_pio_is_done());
    emfi_pio_clear_done();
    TEST_ASSERT_FALSE(emfi_pio_is_done());
}

static void test_ticks_per_us_is_125(void) {
    TEST_ASSERT_EQUAL_UINT32(125u, emfi_pio_ticks_per_us());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_claims_pio0_sm0);
    RUN_TEST(test_init_fails_if_sm0_already_claimed);
    RUN_TEST(test_deinit_unclaims_and_drops_program);
    RUN_TEST(test_load_rejects_zero_width);
    RUN_TEST(test_load_rejects_width_above_max);
    RUN_TEST(test_load_immediate_has_no_trigger_block);
    RUN_TEST(test_load_rising_edge_inserts_two_waits);
    RUN_TEST(test_load_pulse_positive_inserts_three_waits);
    RUN_TEST(test_load_attaches_emfi_pulse_to_pio);
    RUN_TEST(test_load_binds_gp14_to_pio);
    RUN_TEST(test_load_configures_sm_with_correct_pins);
    RUN_TEST(test_start_pushes_delay_then_width_ticks);
    RUN_TEST(test_is_done_polls_irq0);
    RUN_TEST(test_ticks_per_us_is_125);
    return UNITY_END();
}
```

- [ ] **Step 2: Add a service-test helper to `tests/CMakeLists.txt`**

Append after `faultycat_add_driver_test`:

```cmake
# Service tests need driver + service sources compiled natively
# against hal_fake. For F4-3 this is just emfi_pio + emfi_pulse.
function(faultycat_add_service_emfi_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse/emfi_pulse.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_pio.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F4-3 — emfi_pio service unit tests
faultycat_add_service_emfi_test(test_emfi_pio)
```

- [ ] **Step 3: Stage + run**

```bash
git add tests/test_emfi_pio.c tests/CMakeLists.txt
ctest --preset host-tests
```

Expected: all green, 14 binaries (test_emfi_pio new), 98 tests total
(84 + 14 emfi_pio cases — but also +6 in test_emfi_pulse from Task 3.2
= 84 + 6 + 14 = 104). Adjust mental totals if your count differs.

### Task 3.7: Build firmware

- [ ] **Step 1: Build**

```bash
cmake --preset fw-debug
cmake --build --preset fw-debug
```

Expected: UF2 produced. If link fails with undefined references from
`emfi_pulse.c` to `hal/pio.h`, verify `drivers/emfi_pulse/CMakeLists.txt`
pulls `hal_rp2040` transitively (it already does via `PUBLIC hal_rp2040`).
No CMake change needed.

### Task 3.8: **STOP** — request SIGNED commit approval from Sabas

- [ ] **Step 1: Ask Sabas**

Message to Sabas (literal — paste into chat):

> "F4-3 ready to commit. Touches:
> - `drivers/emfi_pulse/{emfi_pulse.h,emfi_pulse.c}` (attach_pio + CPU-fire
>   interlock)
> - `services/glitch_engine/emfi/emfi_pio.{h,c}` (first PIO-path to GP14)
> - New service CMake wiring + 14 new tests + 6 new driver tests.
>
> Per SAFETY.md §1 this commit is SIGNED. Please fill the checklist:
>
> ```
> Safety: HV charger is in DISARMED state at firmware boot. [ ]
> Safety: 60-second auto-disarm is active by default and tested. [ ]
> Safety: plastic shield is installed for any physical test of this change. [ ]
> Safety: operator has a known-good GND reference. [ ]
> Safety: the output SMA is either loaded by an EMFI coil or discharged. [ ]
> Safety: oscilloscope probe is rated for the measured voltage (10x / 100x / differential). [ ]
> Safety: break-before-make on MOSFET gate transitions is preserved by this change. [NA]
> Safety-justification: this commit only changes the EMFI pulse path (GP14) and does not touch crowbar MOSFET gates (GP16/17).
> Safety: build, flash, and verify were performed in person by the maintainer. [ ]
> Signed-off-by: Sabas <sabasjimenez@gmail.com>
> ```
>
> Note: "build, flash, and verify in person" — since F4-3 only adds
> static paths and the CPU fire path is protected by the `s_attached`
> guard, no physical checkpoint is strictly required before F4-6.
> You may mark it `[NA]` with justification "path not yet wired to
> a host-invocable action; end-to-end physical checkpoint runs at
> F4-6 close." Or flash the current UF2 and observe GP14 stays LOW
> + no scope glitch when the PULSE button is pressed with the
> service compiled in but not started — either is acceptable."

- [ ] **Step 2: Wait for filled checklist**

Do not run `git commit` until Sabas replies with the filled-in block.

### Task 3.9: Commit F4-3 with Sabas's signed block

- [ ] **Step 1: Run the commit**

Paste the received checklist into the command below, replacing
`<CHECKLIST_BODY>`:

```bash
git commit -m "$(cat <<'EOF'
feat(F4-3): emfi_pio service + driver PIO attach — first GP14 PIO path

services/glitch_engine/emfi/emfi_pio.{h,c} — hand-authored PIO
program compiled per-trigger (IMMEDIATE / EXT_RISING / EXT_FALLING /
EXT_PULSE_POS). Layout: PULL delay → OUT Y → trigger block →
JMP Y-- self → PULL width → OUT Y → SET high → JMP Y-- self →
SET low → IRQ 0. 125 MHz clkdiv → 125 ticks/µs, ~8 ns jitter.

drivers/emfi_pulse gains emfi_pulse_attach_pio / _detach_pio / _is_
attached_to_pio. While PIO owns GP14, emfi_pulse_fire_manual returns
false so the two fire paths can never contend. Detach returns the pin
to plain-GPIO LOW.

No host-invocable entry yet — that lands in F4-5 (campaign state
machine) and F4-6 (main.c + emfi_proto). Until then the path is
compiled but cold. PULSE button still takes the CPU-fire path.

6 new driver tests, 14 new service tests. 104 tests / 14 binaries.

<CHECKLIST_BODY>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 2: Verify green**

```bash
ctest --preset host-tests
cmake --build --preset fw-debug
```

Expected: all green, UF2 builds.

---

## F4-4 — `emfi_capture` ADC DMA ring (UNSIGNED)

### Task 4.1: Extend `hal/adc.h` with FIFO + DREQ primitives

**Files:**
- Modify: `hal/include/hal/adc.h`
- Modify: `hal/src/rp2040/adc.c`
- Modify: `tests/hal_fake/hal_fake_adc.h`
- Modify: `tests/hal_fake/adc_fake.c`

The current `hal/adc.h` exposes only `read_raw`. `emfi_capture` needs
FIFO setup, 8-bit shift, DREQ, and `adc_run`. Extend minimally.

- [ ] **Step 1: Read current contract**

```bash
cat hal/include/hal/adc.h
```

- [ ] **Step 2: Append to `hal/include/hal/adc.h`** (preserve existing content):

```c
// -----------------------------------------------------------------------------
// F4-4 — FIFO + DREQ surface for the EMFI capture ring. These map 1:1
// to pico-sdk adc_fifo_setup/adc_set_clkdiv/adc_run.
// -----------------------------------------------------------------------------

typedef struct {
    bool     enable_dma;       // route samples to DMA via DREQ_ADC
    bool     shift_to_8bit;    // true = pushes 8-bit samples (matches legacy glitcher.c)
    uint32_t dreq_threshold;   // DREQ asserted when >= threshold samples present
    bool     enable_fifo;
} hal_adc_fifo_cfg_t;

// Configure the ADC FIFO + DREQ source. Must be called after
// hal_adc_select_input (via hal_adc_read_raw once or explicitly if
// a finer init lands later).
void hal_adc_fifo_setup(const hal_adc_fifo_cfg_t *cfg);

// 0 = full-speed ADC (~500 ksps). Higher values divide the sample rate.
void hal_adc_set_clkdiv(uint32_t div);

// Start / stop continuous conversion. While running, samples stream
// into the FIFO (and from there, via DMA, to the ring buffer).
void hal_adc_run(bool enabled);

// Pointer to the ADC FIFO register for DMA transfers. Opaque to
// callers; pass into hal_dma_configure as `src`.
const volatile void *hal_adc_fifo_register(void);

// Select the ADC input channel. Channel 3 corresponds to GP29 on
// RP2040 (target_monitor). Exposed here because emfi_capture selects
// it directly rather than going through target_monitor.
void hal_adc_select_input(uint8_t channel);
```

- [ ] **Step 3: Extend `hal/src/rp2040/adc.c`**

Append after existing functions:

```c
#include "hardware/adc.h"

void hal_adc_fifo_setup(const hal_adc_fifo_cfg_t *cfg) {
    if (!cfg) return;
    adc_fifo_setup(cfg->enable_fifo, cfg->enable_dma,
                   (uint16_t)cfg->dreq_threshold,
                   false, cfg->shift_to_8bit);
}

void hal_adc_set_clkdiv(uint32_t div) {
    adc_set_clkdiv((float)div);
}

void hal_adc_run(bool enabled) {
    adc_run(enabled);
}

const volatile void *hal_adc_fifo_register(void) {
    return (const volatile void *)&adc_hw->fifo;
}

void hal_adc_select_input(uint8_t channel) {
    adc_select_input(channel);
}
```

- [ ] **Step 4: Extend `tests/hal_fake/hal_fake_adc.h`** — read current file first:

```bash
cat tests/hal_fake/hal_fake_adc.h
```

Append to it:

```c
typedef struct {
    bool     fifo_setup_called;
    hal_adc_fifo_cfg_t last_fifo_cfg;
    uint32_t clkdiv;
    bool     running;
    uint32_t run_calls;
    uint8_t  selected_channel;
    uint32_t select_calls;
} hal_fake_adc_extra_t;

extern hal_fake_adc_extra_t hal_fake_adc_extra;
```

- [ ] **Step 5: Extend `tests/hal_fake/adc_fake.c`**

Read current content:

```bash
cat tests/hal_fake/adc_fake.c
```

Append:

```c
hal_fake_adc_extra_t hal_fake_adc_extra;

void hal_adc_fifo_setup(const hal_adc_fifo_cfg_t *cfg) {
    if (!cfg) return;
    hal_fake_adc_extra.fifo_setup_called = true;
    hal_fake_adc_extra.last_fifo_cfg     = *cfg;
}

void hal_adc_set_clkdiv(uint32_t div) {
    hal_fake_adc_extra.clkdiv = div;
}

void hal_adc_run(bool enabled) {
    hal_fake_adc_extra.running = enabled;
    hal_fake_adc_extra.run_calls++;
}

const volatile void *hal_adc_fifo_register(void) {
    static volatile uint8_t fake_fifo;
    return (const volatile void *)&fake_fifo;
}

void hal_adc_select_input(uint8_t channel) {
    hal_fake_adc_extra.selected_channel = channel;
    hal_fake_adc_extra.select_calls++;
}
```

Also extend the existing `hal_fake_adc_reset` (find it in adc_fake.c)
to `memset` `hal_fake_adc_extra` to zero as well. If the reset
function's body is `memset(&hal_fake_adc_states, 0, ...)`, add a
second line right after: `memset(&hal_fake_adc_extra, 0, sizeof(hal_fake_adc_extra));`.

- [ ] **Step 6: Stage + run**

```bash
git add hal/include/hal/adc.h hal/src/rp2040/adc.c \
        tests/hal_fake/hal_fake_adc.h tests/hal_fake/adc_fake.c
ctest --preset host-tests
```

Expected: all prior tests still green (these additions are purely
additive; no existing behavior changed).

### Task 4.2: Write `services/glitch_engine/emfi/emfi_capture.h`

**Files:**
- Create: `services/glitch_engine/emfi/emfi_capture.h`

- [ ] **Step 1: Write**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/glitch_engine/emfi/emfi_capture — 8 KB ADC DMA ring on
// GP29 (ADC ch 3), 8-bit samples at full ADC speed.
//
// Ported in spirit (and in PCB-specific constants) from the legacy
// firmware/c/glitcher/glitcher.c::prepare_adc — FaultyCat-origin code,
// BSD-3 clean. Upgraded to use hal/adc + hal/dma rather than pico-sdk
// directly.
//
// Lifecycle:
//   emfi_capture_init   → claim DMA channel, configure, leave idle
//   emfi_capture_start  → adc_run(true), arm the DMA ring
//   emfi_capture_stop   → adc_run(false), abort DMA, freeze buffer
//   emfi_capture_buffer → stable pointer into the 8 KB ring
//   emfi_capture_fill   → 0..8192, saturates after first full wrap

#define EMFI_CAPTURE_BUFFER_BYTES 8192u

bool emfi_capture_init(void);
void emfi_capture_start(void);
void emfi_capture_stop(void);
const uint8_t *emfi_capture_buffer(void);
uint32_t emfi_capture_fill(void);
```

- [ ] **Step 2: Stage**

```bash
git add services/glitch_engine/emfi/emfi_capture.h
```

### Task 4.3: Write `services/glitch_engine/emfi/emfi_capture.c`

**Files:**
- Create: `services/glitch_engine/emfi/emfi_capture.c`

- [ ] **Step 1: Write**

```c
#include "emfi_capture.h"

#include <string.h>

#include "hal/adc.h"
#include "hal/dma.h"

#define EMFI_CAPTURE_ADC_CHANNEL  3u   // GP29 = target_monitor
#define EMFI_CAPTURE_RING_BITS    13u  // 2^13 = 8192
#define EMFI_CAPTURE_DREQ_THRESH  1u

static uint8_t           s_buffer[EMFI_CAPTURE_BUFFER_BYTES];
static hal_dma_channel_t s_dma   = -1;
static bool              s_init  = false;
static bool              s_running = false;

bool emfi_capture_init(void) {
    if (s_init) return true;
    s_dma = hal_dma_claim_unused();
    if (s_dma < 0) return false;

    hal_adc_select_input(EMFI_CAPTURE_ADC_CHANNEL);
    hal_adc_fifo_setup(&(hal_adc_fifo_cfg_t){
        .enable_fifo      = true,
        .enable_dma       = true,
        .dreq_threshold   = EMFI_CAPTURE_DREQ_THRESH,
        .shift_to_8bit    = true,
    });
    hal_adc_set_clkdiv(0);  // full speed

    hal_dma_cfg_t cfg = {
        .size            = HAL_DMA_SIZE_8,
        .read_increment  = false,
        .write_increment = true,
        .ring_bits       = EMFI_CAPTURE_RING_BITS,
        .ring_on_write   = true,
        .dreq            = HAL_DMA_DREQ_ADC,
    };
    memset(s_buffer, 0, sizeof(s_buffer));
    hal_dma_configure(s_dma, &cfg,
                     s_buffer,
                     hal_adc_fifo_register(),
                     0xFFFFFFFFu,
                     false);
    s_init = true;
    s_running = false;
    return true;
}

void emfi_capture_start(void) {
    if (!s_init || s_running) return;
    // (re-)arm the transfer from offset 0 by restarting the DMA.
    // A fresh configure with start=true resets write_address to s_buffer.
    hal_dma_cfg_t cfg = {
        .size            = HAL_DMA_SIZE_8,
        .read_increment  = false,
        .write_increment = true,
        .ring_bits       = EMFI_CAPTURE_RING_BITS,
        .ring_on_write   = true,
        .dreq            = HAL_DMA_DREQ_ADC,
    };
    hal_dma_configure(s_dma, &cfg,
                     s_buffer,
                     hal_adc_fifo_register(),
                     0xFFFFFFFFu,
                     true);
    hal_adc_run(true);
    s_running = true;
}

void emfi_capture_stop(void) {
    if (!s_init || !s_running) return;
    hal_adc_run(false);
    hal_dma_abort(s_dma);
    s_running = false;
}

const uint8_t *emfi_capture_buffer(void) {
    return s_buffer;
}

uint32_t emfi_capture_fill(void) {
    if (!s_init) return 0;
    uint32_t remaining = hal_dma_transfer_count(s_dma);
    if (remaining == 0xFFFFFFFFu) return 0;
    // Every pushed sample decrements the transfer_count from the
    // starting 0xFFFFFFFF. Once it has decremented by ≥ 8192 the ring
    // has wrapped at least once.
    uint32_t pushed = 0xFFFFFFFFu - remaining;
    if (pushed >= EMFI_CAPTURE_BUFFER_BYTES) return EMFI_CAPTURE_BUFFER_BYTES;
    return pushed;
}
```

- [ ] **Step 2: Update `services/glitch_engine/emfi/CMakeLists.txt`**

Add `emfi_capture.c` to the source list:

```cmake
add_library(service_emfi STATIC
    emfi_pio.c
    emfi_capture.c
)

target_include_directories(service_emfi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_emfi
    PUBLIC
        hal_rp2040
        board_v2_header
        driver_emfi_pulse
)
```

- [ ] **Step 3: Stage**

```bash
git add services/glitch_engine/emfi/emfi_capture.c services/glitch_engine/emfi/CMakeLists.txt
```

### Task 4.4: Write `tests/test_emfi_capture.c`

**Files:**
- Create: `tests/test_emfi_capture.c`

- [ ] **Step 1: Write**

```c
// Unit tests for services/glitch_engine/emfi/emfi_capture.

#include "unity.h"

#include "emfi_capture.h"
#include "hal/dma.h"
#include "hal_fake_adc.h"
#include "hal_fake_dma.h"

void setUp(void) {
    hal_fake_adc_reset();
    hal_fake_dma_reset();
}
void tearDown(void) {
    emfi_capture_stop();
}

static void test_init_claims_one_dma_channel(void) {
    TEST_ASSERT_TRUE(emfi_capture_init());
    int used = 0;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) used++;
    }
    TEST_ASSERT_EQUAL_INT(1, used);
}

static void test_init_selects_adc_channel_3(void) {
    emfi_capture_init();
    TEST_ASSERT_EQUAL_UINT8(3, hal_fake_adc_extra.selected_channel);
}

static void test_init_configures_fifo_for_8bit_dma(void) {
    emfi_capture_init();
    TEST_ASSERT_TRUE(hal_fake_adc_extra.fifo_setup_called);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.last_fifo_cfg.enable_dma);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.last_fifo_cfg.shift_to_8bit);
    TEST_ASSERT_EQUAL_UINT32(1u,
        hal_fake_adc_extra.last_fifo_cfg.dreq_threshold);
}

static void test_init_sets_full_speed_clkdiv(void) {
    emfi_capture_init();
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_adc_extra.clkdiv);
}

static void test_start_arms_dma_and_runs_adc(void) {
    emfi_capture_init();
    emfi_capture_start();
    // One claimed channel is now busy.
    int busy_count = 0;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].busy) busy_count++;
    }
    TEST_ASSERT_EQUAL_INT(1, busy_count);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.running);
}

static void test_start_configures_ring_mode_8192_bytes(void) {
    emfi_capture_init();
    emfi_capture_start();
    int ch = -1;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) { ch = i; break; }
    }
    TEST_ASSERT_NOT_EQUAL(-1, ch);
    TEST_ASSERT_EQUAL_UINT32(13u, hal_fake_dma_channels[ch].cfg.ring_bits);
    TEST_ASSERT_TRUE(hal_fake_dma_channels[ch].cfg.ring_on_write);
    TEST_ASSERT_EQUAL_UINT(HAL_DMA_DREQ_ADC, hal_fake_dma_channels[ch].cfg.dreq);
}

static void test_stop_halts_adc_and_aborts_dma(void) {
    emfi_capture_init();
    emfi_capture_start();
    emfi_capture_stop();
    TEST_ASSERT_FALSE(hal_fake_adc_extra.running);
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) {
            TEST_ASSERT_FALSE(hal_fake_dma_channels[i].busy);
        }
    }
}

static void test_buffer_pointer_is_stable(void) {
    emfi_capture_init();
    const uint8_t *a = emfi_capture_buffer();
    emfi_capture_start();
    const uint8_t *b = emfi_capture_buffer();
    emfi_capture_stop();
    const uint8_t *c = emfi_capture_buffer();
    TEST_ASSERT_EQUAL_PTR(a, b);
    TEST_ASSERT_EQUAL_PTR(b, c);
}

static void test_fill_saturates_at_8192(void) {
    emfi_capture_init();
    emfi_capture_start();
    int ch = -1;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) { ch = i; break; }
    }
    // Simulate 1000 samples pushed.
    hal_fake_dma_set_transfer_count(ch, 0xFFFFFFFFu - 1000u);
    TEST_ASSERT_EQUAL_UINT32(1000u, emfi_capture_fill());
    // Simulate full wrap + then some.
    hal_fake_dma_set_transfer_count(ch, 0xFFFFFFFFu - 20000u);
    TEST_ASSERT_EQUAL_UINT32(8192u, emfi_capture_fill());
}

static void test_init_idempotent(void) {
    TEST_ASSERT_TRUE(emfi_capture_init());
    TEST_ASSERT_TRUE(emfi_capture_init());  // no second claim
    int used = 0;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) used++;
    }
    TEST_ASSERT_EQUAL_INT(1, used);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_claims_one_dma_channel);
    RUN_TEST(test_init_selects_adc_channel_3);
    RUN_TEST(test_init_configures_fifo_for_8bit_dma);
    RUN_TEST(test_init_sets_full_speed_clkdiv);
    RUN_TEST(test_start_arms_dma_and_runs_adc);
    RUN_TEST(test_start_configures_ring_mode_8192_bytes);
    RUN_TEST(test_stop_halts_adc_and_aborts_dma);
    RUN_TEST(test_buffer_pointer_is_stable);
    RUN_TEST(test_fill_saturates_at_8192);
    RUN_TEST(test_init_idempotent);
    return UNITY_END();
}
```

- [ ] **Step 2: Register in `tests/CMakeLists.txt`**

Extend the service-emfi helper to cover multiple source files, then
add the new test. Replace `faultycat_add_service_emfi_test` with:

```cmake
function(faultycat_add_service_emfi_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse/emfi_pulse.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_pio.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_capture.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

faultycat_add_service_emfi_test(test_emfi_pio)
# F4-4 — emfi_capture service unit tests
faultycat_add_service_emfi_test(test_emfi_capture)
```

- [ ] **Step 3: Stage + run**

```bash
git add tests/test_emfi_capture.c tests/CMakeLists.txt
ctest --preset host-tests
```

Expected: all green. `test_emfi_capture` adds 10 cases.

### Task 4.5: Build firmware

- [ ] **Step 1: Build**

```bash
cmake --build --preset fw-debug
```

Expected: UF2 produced.

### Task 4.6: Commit F4-4

- [ ] **Step 1: Stage + commit (UNSIGNED — no HV driver touched)**

```bash
git commit -m "$(cat <<'EOF'
feat(F4-4): emfi_capture — 8 KB ADC DMA ring on GP29 + hal/adc FIFO/DREQ

services/glitch_engine/emfi/emfi_capture — ADC channel 3 (GP29 /
target_monitor), 8-bit samples, full-speed ADC, DMA ring-mode
8192 bytes (ring_bits=13). Start arms DMA from offset 0 and runs
the ADC; stop aborts DMA and halts ADC. Buffer pointer is stable
across lifecycle.

Ported from firmware/c/glitcher/glitcher.c::prepare_adc (FaultyCat
origin, BSD-3 clean) into the new HAL. hal/adc gains fifo_setup,
set_clkdiv, run, select_input, fifo_register — the minimum surface
emfi_capture needs. hal/adc fakes extended with inspectable extra
state.

10 new service tests. Totals now 114 tests / 15 binaries.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 2: Verify**

```bash
ctest --preset host-tests
cmake --build --preset fw-debug
```

Expected: all green, UF2 builds.

---

## F4-5 — `emfi_campaign` state machine (SIGNED)

Single SIGNED commit. Activates SAFETY.md §3 invariant 5 (HV charged
within the last 100 ms before PIO fire). Sabas must approve the
filled-in checklist before the commit runs.

### Carried over from F4-3 code review

F4-3's code review flagged two items that this phase must address in
addition to Tasks 5.1–5.7:

- **PC-reset primitive needed for re-fire.** `emfi_pio_clear_done`
  disables the SM but does not reset the program counter. A subsequent
  `emfi_pio_start` resumes from wherever the program stopped (past
  `IRQ 0`), so the waveform never re-executes from offset 0. F4-5's
  teardown (FIRED → IDLE) calls `emfi_pio_deinit` + next run calls
  `emfi_pio_init` + `emfi_pio_load`, which does fully reload the
  program. That covers the happy path. If F4-5 ever tries to re-fire
  without deinit/reload, it MUST add a PC-reset primitive — either
  `hal_pio_sm_exec(pio, sm, JMP offset)` (new HAL surface) or
  `hal_pio_sm_restart` + `hal_pio_sm_set_enabled(false/true)` with a
  program-start offset push. Pick the approach during F4-5.1; don't
  defer further.

- **`emfi_pio_ticks_per_us` docstring** promises "at the configured
  clock divisor" but the divisor is `#define`d. Fix the docstring or
  expose configurability while you're here.

### Task 5.1: Write `services/glitch_engine/emfi/emfi_campaign.h`

**Files:**
- Create: `services/glitch_engine/emfi/emfi_campaign.h`

- [ ] **Step 1: Write**

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "emfi_pio.h"   // for emfi_trig_t

// services/glitch_engine/emfi/emfi_campaign — orchestrates the
// EMFI fire path end-to-end. Owns the state machine, enforces the
// SAFETY.md §3 item 5 invariant (HV charged within 100 ms before
// PIO start), and teardown on every terminal state.
//
// Drivers/services used (composed in strict layer order):
//   drivers/hv_charger     — arm, poll CHARGED, disarm
//   drivers/ext_trigger    — already initialized by main; read-only
//   services/emfi_pio      — PIO fire path
//   services/emfi_capture  — ADC DMA ring
//   drivers/emfi_pulse     — attach/detach of GP14 (via emfi_pio)
//
// Public API is poll-based: fire() is non-blocking, tick() advances
// the state machine from the main loop. Host callers get status via
// get_status(); capture buffer via capture_buffer()/capture_len().

typedef struct {
    emfi_trig_t trigger;
    uint32_t    delay_us;
    uint32_t    width_us;
    uint32_t    charge_timeout_ms;   // 0 = wait up to hv_charger auto-disarm (60 s)
} emfi_config_t;

typedef enum {
    EMFI_STATE_IDLE    = 0,
    EMFI_STATE_ARMING  = 1,
    EMFI_STATE_CHARGED = 2,
    EMFI_STATE_WAITING = 3,   // trigger wait + PIO running
    EMFI_STATE_FIRED   = 4,
    EMFI_STATE_ERROR   = 5,
} emfi_state_t;

typedef enum {
    EMFI_ERR_NONE             = 0,
    EMFI_ERR_BAD_CONFIG       = 1,
    EMFI_ERR_HV_NOT_CHARGED   = 2,
    EMFI_ERR_TRIGGER_TIMEOUT  = 3,
    EMFI_ERR_PIO_FAULT        = 4,
    EMFI_ERR_INTERNAL         = 5,
} emfi_err_t;

typedef struct {
    emfi_state_t state;
    emfi_err_t   err;
    uint32_t     last_fire_at_ms;
    uint32_t     capture_fill;
    uint32_t     pulse_width_us_actual;
    uint32_t     delay_us_actual;
} emfi_status_t;

// SAFETY constant — the HV-within-100 ms window per SAFETY.md §3 #5.
// Exposed for tests and for diagnostics.
#define EMFI_CAMPAIGN_HV_STALE_MS 100u

bool emfi_campaign_init(void);
bool emfi_campaign_configure(const emfi_config_t *cfg);
bool emfi_campaign_arm(void);
bool emfi_campaign_fire(uint32_t trigger_timeout_ms);
void emfi_campaign_disarm(void);
void emfi_campaign_tick(void);
void emfi_campaign_get_status(emfi_status_t *out);
const uint8_t *emfi_campaign_capture_buffer(void);
uint32_t       emfi_campaign_capture_len(void);
```

- [ ] **Step 2: Stage**

```bash
git add services/glitch_engine/emfi/emfi_campaign.h
```

### Task 5.2: Write `services/glitch_engine/emfi/emfi_campaign.c`

**Files:**
- Create: `services/glitch_engine/emfi/emfi_campaign.c`

- [ ] **Step 1: Write**

```c
#include "emfi_campaign.h"

#include <string.h>

#include "emfi_capture.h"
#include "emfi_pio.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"

static emfi_config_t s_cfg;
static bool          s_cfg_valid = false;
static emfi_status_t s_status;
static uint32_t      s_arm_start_ms;
static uint32_t      s_fire_start_ms;
static uint32_t      s_fire_timeout_ms;
static uint32_t      s_hv_last_charged_ms;

static void reset_status(void) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = EMFI_STATE_IDLE;
}

static void teardown(void) {
    // Always safe to call, regardless of which pieces were actually started.
    emfi_capture_stop();
    emfi_pio_clear_done();
    emfi_pio_deinit();
    emfi_pulse_detach_pio();
    hv_charger_disarm();
}

static void enter_error(emfi_err_t err) {
    teardown();
    s_status.state = EMFI_STATE_ERROR;
    s_status.err   = err;
}

bool emfi_campaign_init(void) {
    reset_status();
    s_cfg_valid        = false;
    s_hv_last_charged_ms = 0;
    return true;
}

bool emfi_campaign_configure(const emfi_config_t *cfg) {
    if (!cfg) return false;
    if (cfg->width_us < 1u || cfg->width_us > 50u) return false;
    if (cfg->delay_us > 1000000u) return false;
    s_cfg       = *cfg;
    s_cfg_valid = true;
    // Clearing any error from a prior run lets a fresh configure
    // drop the service back into IDLE.
    s_status.state = EMFI_STATE_IDLE;
    s_status.err   = EMFI_ERR_NONE;
    return true;
}

bool emfi_campaign_arm(void) {
    if (!s_cfg_valid) {
        enter_error(EMFI_ERR_BAD_CONFIG);
        return false;
    }
    if (s_status.state != EMFI_STATE_IDLE) return false;

    hv_charger_arm();
    s_arm_start_ms = hal_now_ms();
    s_status.state = EMFI_STATE_ARMING;
    s_status.err   = EMFI_ERR_NONE;
    return true;
}

bool emfi_campaign_fire(uint32_t trigger_timeout_ms) {
    if (s_status.state != EMFI_STATE_CHARGED) return false;

    // Re-check invariant right here: if HV was last seen charged too
    // long ago, fail fast rather than firing into a half-spent cap.
    uint32_t now = hal_now_ms();
    if ((now - s_hv_last_charged_ms) > EMFI_CAMPAIGN_HV_STALE_MS) {
        // Resample once in case the bit has returned.
        if (hv_charger_is_charged()) {
            s_hv_last_charged_ms = now;
        } else {
            enter_error(EMFI_ERR_HV_NOT_CHARGED);
            return false;
        }
    }

    if (!emfi_pio_init()) {
        enter_error(EMFI_ERR_PIO_FAULT);
        return false;
    }
    emfi_pio_params_t pp = {
        .trigger  = s_cfg.trigger,
        .delay_us = s_cfg.delay_us,
        .width_us = s_cfg.width_us,
    };
    if (!emfi_pio_load(&pp)) {
        enter_error(EMFI_ERR_PIO_FAULT);
        return false;
    }
    emfi_capture_start();
    if (!emfi_pio_start()) {
        enter_error(EMFI_ERR_PIO_FAULT);
        return false;
    }

    s_fire_start_ms   = now;
    s_fire_timeout_ms = trigger_timeout_ms;
    s_status.state    = EMFI_STATE_WAITING;
    return true;
}

void emfi_campaign_disarm(void) {
    teardown();
    reset_status();
}

static void tick_arming(void) {
    if (hv_charger_is_charged()) {
        s_status.state       = EMFI_STATE_CHARGED;
        s_hv_last_charged_ms = hal_now_ms();
        return;
    }
    uint32_t elapsed = hal_now_ms() - s_arm_start_ms;
    uint32_t timeout = s_cfg.charge_timeout_ms;
    if (timeout == 0u) return;  // 0 = wait up to hv_charger auto-disarm
    if (elapsed > timeout) {
        enter_error(EMFI_ERR_HV_NOT_CHARGED);
    }
}

static void tick_charged(void) {
    // Keep the "last charged" timestamp fresh while sitting in CHARGED
    // state, so the 100 ms invariant at fire() time reflects reality.
    if (hv_charger_is_charged()) {
        s_hv_last_charged_ms = hal_now_ms();
    }
}

static void tick_waiting(void) {
    if (emfi_pio_is_done()) {
        s_status.capture_fill         = emfi_capture_fill();
        s_status.pulse_width_us_actual = s_cfg.width_us;
        s_status.delay_us_actual      = s_cfg.delay_us;
        s_status.last_fire_at_ms      = hal_now_ms();
        teardown();
        s_status.state = EMFI_STATE_FIRED;
        return;
    }
    if (s_fire_timeout_ms == 0u) return;
    uint32_t elapsed = hal_now_ms() - s_fire_start_ms;
    if (elapsed > s_fire_timeout_ms) {
        enter_error(EMFI_ERR_TRIGGER_TIMEOUT);
    }
}

void emfi_campaign_tick(void) {
    hv_charger_tick();  // let the HV driver run its 60 s safety net.
    switch (s_status.state) {
        case EMFI_STATE_ARMING:  tick_arming();  break;
        case EMFI_STATE_CHARGED: tick_charged(); break;
        case EMFI_STATE_WAITING: tick_waiting(); break;
        default: break;
    }
}

void emfi_campaign_get_status(emfi_status_t *out) {
    if (!out) return;
    *out = s_status;
}

const uint8_t *emfi_campaign_capture_buffer(void) {
    return emfi_capture_buffer();
}

uint32_t emfi_campaign_capture_len(void) {
    return s_status.capture_fill;
}
```

- [ ] **Step 2: Update `services/glitch_engine/emfi/CMakeLists.txt`**

```cmake
add_library(service_emfi STATIC
    emfi_pio.c
    emfi_capture.c
    emfi_campaign.c
)

target_include_directories(service_emfi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_emfi
    PUBLIC
        hal_rp2040
        board_v2_header
        driver_emfi_pulse
        driver_hv_charger
        driver_ext_trigger
)
```

- [ ] **Step 3: Stage**

```bash
git add services/glitch_engine/emfi/emfi_campaign.c services/glitch_engine/emfi/CMakeLists.txt
```

### Task 5.3: Write `tests/test_emfi_campaign.c`

**Files:**
- Create: `tests/test_emfi_campaign.c`

- [ ] **Step 1: Write**

```c
// Unit tests for services/glitch_engine/emfi/emfi_campaign.

#include "unity.h"

#include "board_v2.h"
#include "emfi_campaign.h"
#include "emfi_capture.h"
#include "emfi_pio.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"
#include "hal_fake_adc.h"
#include "hal_fake_dma.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "hal_fake_pwm.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
    hal_fake_adc_reset();
    hal_fake_dma_reset();
    hal_fake_pio_reset();
    hal_fake_pwm_reset();
    hv_charger_init();
    emfi_pulse_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    emfi_campaign_init();
}
void tearDown(void) {
    emfi_campaign_disarm();
}

// Helper: fake HV charger "charged" by poking the GP18 input state.
// hv_charger_is_charged reads GP18 through hal_gpio; level=false
// means logical CHARGED because the sense line is active-low.
static void set_charged(bool on) {
    hal_fake_gpio_states[BOARD_GP_HV_CHARGED].level = !on;
}

static void advance_ms(uint32_t ms) {
    hal_fake_time_advance_ms(ms);
}

// -----------------------------------------------------------------------------

static void test_initial_state_is_idle(void) {
    emfi_status_t s;
    emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_IDLE, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_NONE, s.err);
}

static void test_configure_rejects_zero_width(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 0, .width_us = 0, .charge_timeout_ms = 1000 };
    TEST_ASSERT_FALSE(emfi_campaign_configure(&c));
}

static void test_configure_rejects_width_above_50(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 0, .width_us = 51, .charge_timeout_ms = 1000 };
    TEST_ASSERT_FALSE(emfi_campaign_configure(&c));
}

static void test_arm_without_configure_errors(void) {
    TEST_ASSERT_FALSE(emfi_campaign_arm());
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_BAD_CONFIG, s.err);
}

static void test_arm_transitions_to_arming_and_powers_hv(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    TEST_ASSERT_TRUE(emfi_campaign_arm());
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ARMING, s.state);
    TEST_ASSERT_TRUE(hv_charger_is_armed());
}

static void test_tick_promotes_arming_to_charged_on_sense(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_CHARGED, s.state);
}

static void test_charge_timeout_flips_to_error(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 100 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    // Never assert CHARGED.
    advance_ms(150);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_HV_NOT_CHARGED, s.err);
    TEST_ASSERT_FALSE(hv_charger_is_armed());   // teardown disarmed.
}

static void test_fire_from_charged_enters_waiting_and_starts_pio(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    TEST_ASSERT_TRUE(emfi_campaign_fire(500));
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_WAITING, s.state);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[0].enabled);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.running);
}

static void test_hv_stale_100ms_invariant_blocks_fire(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    // Lose CHARGED and age past the window.
    set_charged(false);
    advance_ms(150);
    TEST_ASSERT_FALSE(emfi_campaign_fire(500));
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_HV_NOT_CHARGED, s.err);
}

static void test_waiting_completes_on_pio_irq0(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(500);
    // Simulate PIO finishing.
    hal_fake_pio_raise_irq(0, 0);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_FIRED, s.state);
    TEST_ASSERT_FALSE(hv_charger_is_armed());
}

static void test_waiting_times_out_if_pio_never_signals(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_EXT_RISING,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(50);
    advance_ms(100);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_TRIGGER_TIMEOUT, s.err);
}

static void test_disarm_from_any_state_returns_to_idle(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(500);
    emfi_campaign_disarm();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_IDLE, s.state);
    TEST_ASSERT_FALSE(hv_charger_is_armed());
    TEST_ASSERT_FALSE(emfi_pulse_is_attached_to_pio());
}

static void test_reconfigure_clears_error_state(void) {
    // First, drive into ERROR via arm-without-configure.
    TEST_ASSERT_FALSE(emfi_campaign_arm());
    // Now configure a valid one.
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    TEST_ASSERT_TRUE(emfi_campaign_configure(&c));
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_IDLE, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_NONE, s.err);
}

static void test_fire_records_capture_fill(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(500);
    // Simulate DMA having written 2048 bytes by decrementing
    // transfer_count on the only claimed channel.
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) {
            hal_fake_dma_set_transfer_count(i, 0xFFFFFFFFu - 2048u);
            break;
        }
    }
    hal_fake_pio_raise_irq(0, 0);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_FIRED, s.state);
    TEST_ASSERT_EQUAL_UINT32(2048u, s.capture_fill);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_configure_rejects_zero_width);
    RUN_TEST(test_configure_rejects_width_above_50);
    RUN_TEST(test_arm_without_configure_errors);
    RUN_TEST(test_arm_transitions_to_arming_and_powers_hv);
    RUN_TEST(test_tick_promotes_arming_to_charged_on_sense);
    RUN_TEST(test_charge_timeout_flips_to_error);
    RUN_TEST(test_fire_from_charged_enters_waiting_and_starts_pio);
    RUN_TEST(test_hv_stale_100ms_invariant_blocks_fire);
    RUN_TEST(test_waiting_completes_on_pio_irq0);
    RUN_TEST(test_waiting_times_out_if_pio_never_signals);
    RUN_TEST(test_disarm_from_any_state_returns_to_idle);
    RUN_TEST(test_reconfigure_clears_error_state);
    RUN_TEST(test_fire_records_capture_fill);
    return UNITY_END();
}
```

- [ ] **Step 2: Extend `tests/CMakeLists.txt` to link hv_charger + ext_trigger sources into emfi campaign tests**

Replace `faultycat_add_service_emfi_test` with a richer helper:

```cmake
function(faultycat_add_service_emfi_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse/emfi_pulse.c
        ${CMAKE_SOURCE_DIR}/drivers/hv_charger/hv_charger.c
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger/ext_trigger.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_pio.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_capture.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_campaign.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse
        ${CMAKE_SOURCE_DIR}/drivers/hv_charger
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

faultycat_add_service_emfi_test(test_emfi_pio)
faultycat_add_service_emfi_test(test_emfi_capture)
# F4-5 — emfi_campaign state machine
faultycat_add_service_emfi_test(test_emfi_campaign)
```

- [ ] **Step 3: Stage + run**

```bash
git add tests/test_emfi_campaign.c tests/CMakeLists.txt
ctest --preset host-tests
```

Expected: all green. `test_emfi_campaign` adds 14 cases.

### Task 5.4: Activate SAFETY.md §3 invariant 5

**Files:**
- Modify: `docs/SAFETY.md`

- [ ] **Step 1: Edit SAFETY.md**

Find the existing item 5 in §3:

```
5. `emfi_pulse` (F2b-4) will require (a) `hv_charger_is_charged()`
   to have been true at least once in the last 100 ms, and (b) an
   explicit arm-token issued by the app layer. This invariant is
   planned for F2b-4 — the checklist item for it gets activated in
   that commit.
```

Replace with:

```
5. `services/glitch_engine/emfi/emfi_campaign` enforces: PIO-driven
   fire is blocked unless `hv_charger_is_charged()` was observed
   true within the last `EMFI_CAMPAIGN_HV_STALE_MS` (100 ms) window
   at `emfi_campaign_fire` time. On miss, the service transitions
   to ERROR(HV_NOT_CHARGED) and tears down. Activated in F4-5 commit.
   The invariant covers the PIO fire path only; the CPU-timed
   `emfi_pulse_fire_manual` (button path) remains gated by the
   operator's in-person judgment.
```

- [ ] **Step 2: Stage**

```bash
git add docs/SAFETY.md
```

### Task 5.5: Build firmware

- [ ] **Step 1: Build**

```bash
cmake --build --preset fw-debug
```

Expected: UF2 builds cleanly.

### Task 5.6: **STOP** — request SIGNED checklist for F4-5

- [ ] **Step 1: Ask Sabas**

> "F4-5 ready to commit. Touches:
> - `services/glitch_engine/emfi/emfi_campaign.{h,c}` — full EMFI state machine.
> - `services/glitch_engine/emfi/CMakeLists.txt` — adds hv_charger + ext_trigger deps.
> - `docs/SAFETY.md` §3 item 5 — invariant activated (100 ms HV window).
> - `tests/test_emfi_campaign.c` (14 new cases) + test helper update.
>
> No direct change to `hv_charger`/`emfi_pulse`/`pwm.c` this commit, but
> the full orchestration path is live — any bug here is a safety bug.
> SIGNED per SAFETY.md §1. Please fill:
>
> ```
> Safety: HV charger is in DISARMED state at firmware boot. [ ]
> Safety: 60-second auto-disarm is active by default and tested. [ ]
> Safety: plastic shield is installed for any physical test of this change. [NA]
> Safety: operator has a known-good GND reference. [NA]
> Safety: the output SMA is either loaded by an EMFI coil or discharged. [NA]
> Safety: oscilloscope probe is rated for the measured voltage (10x / 100x / differential). [NA]
> Safety: break-before-make on MOSFET gate transitions is preserved by this change. [NA]
> Safety-justification: this commit adds the state-machine orchestration; no physical invocation path yet (host CDC0 lands in F4-6). Physical checkpoint runs at F4-6 close.
> Safety: build, flash, and verify were performed in person by the maintainer. [y/NA]
> Signed-off-by: Sabas <sabasjimenez@gmail.com>
> ```"

- [ ] **Step 2: Wait for filled block before commit**

### Task 5.7: Commit F4-5

- [ ] **Step 1: Commit with signed block**

```bash
git commit -m "$(cat <<'EOF'
feat(F4-5): emfi_campaign state machine + 100ms HV invariant activated

services/glitch_engine/emfi/emfi_campaign — IDLE→ARMING→CHARGED
→WAITING→FIRED, with ERROR(HV_NOT_CHARGED|TRIGGER_TIMEOUT|PIO_FAULT
|BAD_CONFIG) paths. tick() is poll-driven from the main loop;
fire() is non-blocking; disarm() is a teardown sink that works from
every state.

SAFETY.md §3 invariant 5 flipped from "planned for F2b-4" to
"activated in F4-5". PIO fire is blocked unless hv_charger_is_charged
was true within EMFI_CAMPAIGN_HV_STALE_MS (100 ms). On miss → ERROR
+ full teardown + hv_charger_disarm.

Every terminal state (FIRED or ERROR) unconditionally runs:
emfi_capture_stop → emfi_pio_clear_done → emfi_pio_deinit →
emfi_pulse_detach_pio → hv_charger_disarm.

14 new cases cover happy path, HV timeout, HV-stale block, trigger
timeout, disarm-from-any-state, reconfigure-clears-error, capture
fill reporting. Totals now 128 tests / 16 binaries.

<CHECKLIST_BODY>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 2: Verify green**

```bash
ctest --preset host-tests
cmake --build --preset fw-debug
```

---

## F4-6 — `emfi_proto` + main.c integration + docs + tag (SIGNED)

Single SIGNED commit. Closes F4 with annotated tag `v3.0-f4`.

### Task 6.1: Write `services/host_proto/emfi_proto/emfi_proto.h`

**Files:**
- Create: `services/host_proto/emfi_proto/emfi_proto.h`

- [ ] **Step 1: Write**

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// services/host_proto/emfi_proto — binary framing for the CDC0
// ("EMFI Control") link between host and the emfi_campaign service.
//
// Frame layout:
//   [0]    SOF = 0xFA
//   [1]    CMD (host→device) or CMD|0x80 (device→host reply)
//   [2..3] LEN (little-endian, 0..512)
//   [4..]  PAYLOAD
//   [4+LEN..] CRC-16/CCITT (poly 0x1021, init 0xFFFF), LE
//
// 100 ms inter-byte timeout resets the parser.

#define EMFI_PROTO_SOF            0xFAu
#define EMFI_PROTO_MAX_PAYLOAD    512u
#define EMFI_PROTO_INTERBYTE_MS   100u

typedef enum {
    EMFI_CMD_PING      = 0x01,
    EMFI_CMD_CONFIGURE = 0x10,
    EMFI_CMD_ARM       = 0x11,
    EMFI_CMD_FIRE      = 0x12,
    EMFI_CMD_DISARM    = 0x13,
    EMFI_CMD_STATUS    = 0x14,
    EMFI_CMD_CAPTURE   = 0x15,
} emfi_cmd_t;

// Initialize parser state.
void emfi_proto_init(void);

// Feed one byte into the parser. Returns true iff a complete,
// CRC-valid frame was just assembled; in that case call
// `emfi_proto_dispatch` to act on it. On parse error (bad SOF,
// LEN overflow, CRC mismatch, inter-byte timeout) the state is reset.
bool emfi_proto_feed(uint8_t byte, uint32_t now_ms);

// Act on the last-assembled frame. Writes the reply frame (including
// SOF+CMD|0x80+LEN+PAYLOAD+CRC) into `reply` and returns its length.
// Returns 0 if no reply is to be sent.
size_t emfi_proto_dispatch(uint8_t *reply, size_t reply_cap);

// CRC-16/CCITT helper exposed for tests.
uint16_t emfi_proto_crc16(const uint8_t *data, size_t len);
```

- [ ] **Step 2: Stage**

```bash
git add services/host_proto/emfi_proto/emfi_proto.h
```

### Task 6.2: Write `services/host_proto/emfi_proto/emfi_proto.c`

**Files:**
- Create: `services/host_proto/emfi_proto/emfi_proto.c`

- [ ] **Step 1: Write**

```c
#include "emfi_proto.h"

#include <string.h>

#include "emfi_campaign.h"

// Parser state ---------------------------------------------------------------

typedef enum {
    S_SOF = 0,
    S_CMD,
    S_LEN_LO,
    S_LEN_HI,
    S_PAYLOAD,
    S_CRC_LO,
    S_CRC_HI,
} parse_state_t;

static parse_state_t s_state;
static uint8_t       s_cmd;
static uint16_t      s_len;
static uint16_t      s_payload_pos;
static uint8_t       s_payload[EMFI_PROTO_MAX_PAYLOAD];
static uint16_t      s_crc_recv;
static uint32_t      s_last_byte_ms;

static bool          s_frame_ready;
static uint8_t       s_frame_cmd;
static uint16_t      s_frame_len;
static uint8_t       s_frame_payload[EMFI_PROTO_MAX_PAYLOAD];

// CRC-16/CCITT ---------------------------------------------------------------

uint16_t emfi_proto_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void reset_parser(void) {
    s_state       = S_SOF;
    s_cmd         = 0;
    s_len         = 0;
    s_payload_pos = 0;
    s_crc_recv    = 0;
}

void emfi_proto_init(void) {
    reset_parser();
    s_frame_ready = false;
    s_last_byte_ms = 0;
}

bool emfi_proto_feed(uint8_t byte, uint32_t now_ms) {
    if (s_state != S_SOF
     && (now_ms - s_last_byte_ms) > EMFI_PROTO_INTERBYTE_MS) {
        reset_parser();
    }
    s_last_byte_ms = now_ms;

    switch (s_state) {
        case S_SOF:
            if (byte == EMFI_PROTO_SOF) s_state = S_CMD;
            return false;
        case S_CMD:
            s_cmd   = byte;
            s_state = S_LEN_LO;
            return false;
        case S_LEN_LO:
            s_len   = byte;
            s_state = S_LEN_HI;
            return false;
        case S_LEN_HI:
            s_len |= (uint16_t)byte << 8;
            if (s_len > EMFI_PROTO_MAX_PAYLOAD) { reset_parser(); return false; }
            s_payload_pos = 0;
            s_state = (s_len == 0) ? S_CRC_LO : S_PAYLOAD;
            return false;
        case S_PAYLOAD:
            s_payload[s_payload_pos++] = byte;
            if (s_payload_pos >= s_len) s_state = S_CRC_LO;
            return false;
        case S_CRC_LO:
            s_crc_recv = byte;
            s_state = S_CRC_HI;
            return false;
        case S_CRC_HI: {
            s_crc_recv |= (uint16_t)byte << 8;
            uint8_t hdr[3] = { s_cmd, (uint8_t)(s_len & 0xFFu),
                               (uint8_t)((s_len >> 8) & 0xFFu) };
            uint16_t calc = emfi_proto_crc16(hdr, 3);
            // Continue CRC over payload
            for (uint16_t i = 0; i < s_len; i++) {
                uint16_t crc = calc;
                crc ^= (uint16_t)s_payload[i] << 8;
                for (int b = 0; b < 8; b++) {
                    crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                          : (uint16_t)(crc << 1);
                }
                calc = crc;
            }
            bool ok = (calc == s_crc_recv);
            if (ok) {
                s_frame_cmd = s_cmd;
                s_frame_len = s_len;
                memcpy(s_frame_payload, s_payload, s_len);
                s_frame_ready = true;
            }
            reset_parser();
            return ok;
        }
    }
    return false;
}

// Writer ---------------------------------------------------------------------

static size_t write_frame(uint8_t *out, size_t cap,
                          uint8_t cmd_reply, const uint8_t *payload, uint16_t len) {
    if (len > EMFI_PROTO_MAX_PAYLOAD) return 0;
    size_t needed = 1u + 1u + 2u + (size_t)len + 2u;
    if (cap < needed) return 0;
    out[0] = EMFI_PROTO_SOF;
    out[1] = cmd_reply;
    out[2] = (uint8_t)(len & 0xFFu);
    out[3] = (uint8_t)((len >> 8) & 0xFFu);
    if (len) memcpy(&out[4], payload, len);
    uint16_t crc = emfi_proto_crc16(&out[1], 3u + (size_t)len);
    out[4 + len]     = (uint8_t)(crc & 0xFFu);
    out[4 + len + 1] = (uint8_t)((crc >> 8) & 0xFFu);
    return needed;
}

// Payload packing helpers ----------------------------------------------------

static void pack_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t unpack_u32_le(const uint8_t *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint16_t unpack_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Dispatch -------------------------------------------------------------------

size_t emfi_proto_dispatch(uint8_t *reply, size_t reply_cap) {
    if (!s_frame_ready || !reply) return 0;
    s_frame_ready = false;

    uint8_t  rpl[64];
    uint16_t rpl_len = 0;
    uint8_t  err     = EMFI_ERR_NONE;

    switch (s_frame_cmd) {
        case EMFI_CMD_PING: {
            static const uint8_t pong[] = { 'F','4',0,0 };
            memcpy(rpl, pong, sizeof(pong));
            rpl_len = (uint16_t)sizeof(pong);
            break;
        }
        case EMFI_CMD_CONFIGURE: {
            if (s_frame_len < 1u + 4u + 4u + 4u) { err = EMFI_ERR_BAD_CONFIG; break; }
            emfi_config_t c = {
                .trigger           = (emfi_trig_t)s_frame_payload[0],
                .delay_us          = unpack_u32_le(&s_frame_payload[1]),
                .width_us          = unpack_u32_le(&s_frame_payload[5]),
                .charge_timeout_ms = unpack_u32_le(&s_frame_payload[9]),
            };
            if (!emfi_campaign_configure(&c)) err = EMFI_ERR_BAD_CONFIG;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case EMFI_CMD_ARM: {
            if (!emfi_campaign_arm()) err = EMFI_ERR_BAD_CONFIG;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case EMFI_CMD_FIRE: {
            if (s_frame_len < 4u) { err = EMFI_ERR_BAD_CONFIG; rpl[0] = err; rpl_len = 1; break; }
            uint32_t to = unpack_u32_le(s_frame_payload);
            if (!emfi_campaign_fire(to)) err = EMFI_ERR_INTERNAL;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case EMFI_CMD_DISARM: {
            emfi_campaign_disarm();
            rpl[0] = EMFI_ERR_NONE; rpl_len = 1;
            break;
        }
        case EMFI_CMD_STATUS: {
            emfi_status_t s; emfi_campaign_get_status(&s);
            rpl[0] = (uint8_t)s.state;
            rpl[1] = (uint8_t)s.err;
            pack_u32_le(&rpl[2],  s.last_fire_at_ms);
            pack_u32_le(&rpl[6],  s.capture_fill);
            pack_u32_le(&rpl[10], s.pulse_width_us_actual);
            pack_u32_le(&rpl[14], s.delay_us_actual);
            rpl_len = 18;
            break;
        }
        case EMFI_CMD_CAPTURE: {
            if (s_frame_len < 4u) { err = EMFI_ERR_BAD_CONFIG; rpl[0] = err; rpl_len = 1; break; }
            uint16_t off = unpack_u16_le(&s_frame_payload[0]);
            uint16_t len = unpack_u16_le(&s_frame_payload[2]);
            if (len > 512u) len = 512u;
            if ((uint32_t)off + len > 8192u) {
                err = EMFI_ERR_BAD_CONFIG; rpl[0] = err; rpl_len = 1; break;
            }
            const uint8_t *buf = emfi_campaign_capture_buffer();
            return write_frame(reply, reply_cap,
                              (uint8_t)(s_frame_cmd | 0x80u),
                              &buf[off], len);
        }
        default:
            err = EMFI_ERR_BAD_CONFIG;
            rpl[0] = err;
            rpl_len = 1;
            break;
    }

    return write_frame(reply, reply_cap,
                      (uint8_t)(s_frame_cmd | 0x80u), rpl, rpl_len);
}
```

- [ ] **Step 2: Write `services/host_proto/CMakeLists.txt`**

```cmake
add_subdirectory(emfi_proto)
# crowbar_proto in F5, campaign_proto in F9
```

- [ ] **Step 3: Write `services/host_proto/emfi_proto/CMakeLists.txt`**

```cmake
add_library(host_proto_emfi STATIC
    emfi_proto.c
)
target_include_directories(host_proto_emfi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(host_proto_emfi
    PUBLIC
        service_emfi
)
```

- [ ] **Step 4: Update `services/CMakeLists.txt`** — uncomment the host_proto line:

```cmake
add_subdirectory(glitch_engine)
add_subdirectory(host_proto)
```

- [ ] **Step 5: Stage**

```bash
git add services/host_proto/emfi_proto/emfi_proto.c \
        services/host_proto/CMakeLists.txt \
        services/host_proto/emfi_proto/CMakeLists.txt \
        services/CMakeLists.txt
```

### Task 6.3: Write `tests/test_emfi_proto.c`

**Files:**
- Create: `tests/test_emfi_proto.c`

- [ ] **Step 1: Write**

```c
// Unit tests for services/host_proto/emfi_proto.

#include "unity.h"

#include <string.h>

#include "board_v2.h"
#include "emfi_campaign.h"
#include "emfi_proto.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal_fake_adc.h"
#include "hal_fake_dma.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "hal_fake_pwm.h"
#include "hal_fake_time.h"
#include "hv_charger.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
    hal_fake_adc_reset();
    hal_fake_dma_reset();
    hal_fake_pio_reset();
    hal_fake_pwm_reset();
    hv_charger_init();
    emfi_pulse_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    emfi_campaign_init();
    emfi_proto_init();
}
void tearDown(void) {
    emfi_campaign_disarm();
}

static bool feed_frame(const uint8_t *bytes, size_t n) {
    bool ready = false;
    for (size_t i = 0; i < n; i++) {
        ready = emfi_proto_feed(bytes[i], 0u);
    }
    return ready;
}

static void append_crc(uint8_t *frame, uint16_t body_len) {
    uint16_t crc = emfi_proto_crc16(&frame[1], 3u + body_len);
    frame[4 + body_len]     = (uint8_t)crc;
    frame[4 + body_len + 1] = (uint8_t)(crc >> 8);
}

static void test_crc_known_value(void) {
    // CRC-16/CCITT of "123456789" = 0x29B1.
    TEST_ASSERT_EQUAL_HEX16(0x29B1u,
        emfi_proto_crc16((const uint8_t *)"123456789", 9));
}

static void test_ping_assembles_and_replies(void) {
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(10u, n);  // SOF + CMD + 2 LEN + 4 payload + 2 CRC
    TEST_ASSERT_EQUAL_UINT8(EMFI_PROTO_SOF, reply[0]);
    TEST_ASSERT_EQUAL_UINT8(EMFI_CMD_PING | 0x80u, reply[1]);
    TEST_ASSERT_EQUAL_UINT8('F', reply[4]);
    TEST_ASSERT_EQUAL_UINT8('4', reply[5]);
}

static void test_bad_sof_is_ignored(void) {
    TEST_ASSERT_FALSE(emfi_proto_feed(0x00u, 0u));
    TEST_ASSERT_FALSE(emfi_proto_feed(0xFFu, 0u));
}

static void test_bad_crc_is_rejected(void) {
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0xAA, 0xBB };
    TEST_ASSERT_FALSE(feed_frame(frame, sizeof(frame)));
}

static void test_len_overflow_resets_parser(void) {
    uint8_t hdr[4] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0x01, 0x03 };  // len=769
    for (size_t i = 0; i < sizeof(hdr); i++) emfi_proto_feed(hdr[i], 0u);
    // Parser must be back at SOF; a new SOF should start a fresh frame.
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
}

static void test_inter_byte_timeout_resets_parser(void) {
    emfi_proto_feed(EMFI_PROTO_SOF, 0u);
    emfi_proto_feed(EMFI_CMD_PING, 50u);
    // Gap of 200 ms > 100 ms inter-byte timeout.
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    bool ok = false;
    ok = emfi_proto_feed(frame[0], 250u);
    TEST_ASSERT_FALSE(ok);
    for (size_t i = 1; i < sizeof(frame); i++) {
        ok = emfi_proto_feed(frame[i], 250u + i);
    }
    TEST_ASSERT_TRUE(ok);
}

static void test_configure_cmd_validates_and_acks(void) {
    // payload: trigger(1) + delay_us(4) + width_us(4) + charge_timeout_ms(4)
    uint8_t frame[4 + 13 + 2];
    frame[0] = EMFI_PROTO_SOF;
    frame[1] = EMFI_CMD_CONFIGURE;
    frame[2] = 13;
    frame[3] = 0;
    frame[4] = EMFI_TRIG_IMMEDIATE;
    // delay_us = 100
    frame[5]  = 100; frame[6]  = 0; frame[7]  = 0; frame[8]  = 0;
    // width_us = 5
    frame[9]  = 5;   frame[10] = 0; frame[11] = 0; frame[12] = 0;
    // charge_timeout_ms = 1000
    frame[13] = 0xE8; frame[14] = 0x03; frame[15] = 0; frame[16] = 0;
    append_crc(frame, 13);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);  // SOF+CMD+2LEN+1err+2CRC
    TEST_ASSERT_EQUAL_UINT8(EMFI_CMD_CONFIGURE | 0x80u, reply[1]);
    TEST_ASSERT_EQUAL_UINT8(EMFI_ERR_NONE, reply[4]);
}

static void test_configure_rejects_width_51(void) {
    uint8_t frame[4 + 13 + 2];
    frame[0] = EMFI_PROTO_SOF;
    frame[1] = EMFI_CMD_CONFIGURE;
    frame[2] = 13; frame[3] = 0;
    frame[4] = EMFI_TRIG_IMMEDIATE;
    frame[5]  = 100; frame[6]  = 0; frame[7]  = 0; frame[8]  = 0;
    frame[9]  = 51;  frame[10] = 0; frame[11] = 0; frame[12] = 0;
    frame[13] = 0;   frame[14] = 0; frame[15] = 0; frame[16] = 0;
    append_crc(frame, 13);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(EMFI_ERR_BAD_CONFIG, reply[4]);
}

static void test_status_payload_is_18_bytes(void) {
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_STATUS, 0, 0, 0, 0 };
    append_crc(frame, 0);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(24u, n);  // 4 hdr + 18 payload + 2 CRC
    TEST_ASSERT_EQUAL_UINT8(18u, reply[2]);
    TEST_ASSERT_EQUAL_UINT8(0u,  reply[3]);
    TEST_ASSERT_EQUAL_UINT8(EMFI_STATE_IDLE, reply[4]);
}

static void test_capture_rejects_out_of_range(void) {
    uint8_t frame[4 + 4 + 2];
    frame[0] = EMFI_PROTO_SOF;
    frame[1] = EMFI_CMD_CAPTURE;
    frame[2] = 4; frame[3] = 0;
    // offset 8000 + len 512 = 8512 > 8192
    frame[4] = 0x40; frame[5] = 0x1F;
    frame[6] = 0x00; frame[7] = 0x02;
    append_crc(frame, 4);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(EMFI_ERR_BAD_CONFIG, reply[4]);
}

static void test_fuzz_random_bytes_never_crash(void) {
    // Seed-deterministic pseudo-random stream. We only need that
    // feed_frame never reads beyond its buffer + that dispatch after
    // each "true" return is safe. This is a smoke test, not a proof.
    uint32_t x = 0xC0FFEEu;
    for (int i = 0; i < 10000; i++) {
        x = x * 1664525u + 1013904223u;
        uint8_t b = (uint8_t)(x >> 17);
        if (emfi_proto_feed(b, (uint32_t)i)) {
            uint8_t reply[768];
            (void)emfi_proto_dispatch(reply, sizeof(reply));
        }
    }
    TEST_ASSERT_TRUE(true);  // reached here = no crash
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_known_value);
    RUN_TEST(test_ping_assembles_and_replies);
    RUN_TEST(test_bad_sof_is_ignored);
    RUN_TEST(test_bad_crc_is_rejected);
    RUN_TEST(test_len_overflow_resets_parser);
    RUN_TEST(test_inter_byte_timeout_resets_parser);
    RUN_TEST(test_configure_cmd_validates_and_acks);
    RUN_TEST(test_configure_rejects_width_51);
    RUN_TEST(test_status_payload_is_18_bytes);
    RUN_TEST(test_capture_rejects_out_of_range);
    RUN_TEST(test_fuzz_random_bytes_never_crash);
    return UNITY_END();
}
```

- [ ] **Step 2: Extend `faultycat_add_service_emfi_test` to also compile emfi_proto.c**

In `tests/CMakeLists.txt` add `emfi_proto.c` to the helper's sources:

```cmake
function(faultycat_add_service_emfi_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse/emfi_pulse.c
        ${CMAKE_SOURCE_DIR}/drivers/hv_charger/hv_charger.c
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger/ext_trigger.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_pio.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_capture.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_campaign.c
        ${CMAKE_SOURCE_DIR}/services/host_proto/emfi_proto/emfi_proto.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse
        ${CMAKE_SOURCE_DIR}/drivers/hv_charger
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi
        ${CMAKE_SOURCE_DIR}/services/host_proto/emfi_proto
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()
```

Append the new test registration:

```cmake
# F4-6 — emfi_proto host framing
faultycat_add_service_emfi_test(test_emfi_proto)
```

- [ ] **Step 3: Stage + run**

```bash
git add tests/test_emfi_proto.c tests/CMakeLists.txt
ctest --preset host-tests
```

Expected: all green. `test_emfi_proto` adds 11 cases.

### Task 6.4: Integrate into `apps/faultycat_fw/main.c`

**Files:**
- Modify: `apps/faultycat_fw/main.c`
- Modify: `apps/faultycat_fw/CMakeLists.txt`

- [ ] **Step 1: Extend `apps/faultycat_fw/CMakeLists.txt`**

Add service libraries to the link list:

```cmake
target_link_libraries(faultycat PRIVATE
    hal_rp2040
    driver_ui_leds
    driver_ui_buttons
    driver_target_monitor
    driver_scanner_io
    driver_ext_trigger
    driver_crowbar_mosfet
    driver_hv_charger
    driver_emfi_pulse
    service_emfi
    host_proto_emfi
    usb_composite
)
```

- [ ] **Step 2: Edit `main.c` — add includes and service hooks**

Add these includes under the existing block:

```c
#include "emfi_campaign.h"
#include "emfi_proto.h"
```

Add this helper above `main()`:

```c
// Pump CDC0 bytes through emfi_proto. Writes any reply to CDC0
// without blocking.
static void pump_emfi_cdc(void) {
    uint8_t buf[64];
    // We reuse tinyusb's read interface via usb_composite; a small
    // internal shim keeps usb_composite as the only owner of
    // tud_cdc_n_read.
    size_t n = usb_composite_cdc_read(USB_CDC_EMFI, buf, sizeof(buf));
    if (n == 0) return;
    for (size_t i = 0; i < n; i++) {
        if (emfi_proto_feed(buf[i], hal_now_ms())) {
            uint8_t reply[768];
            size_t rn = emfi_proto_dispatch(reply, sizeof(reply));
            if (rn > 0) {
                usb_composite_cdc_write(USB_CDC_EMFI, reply, rn);
            }
        }
    }
}
```

In `main()`, after `emfi_pulse_init();` add:

```c
    emfi_campaign_init();
    emfi_proto_init();
```

Inside the `while (true)` loop, after `usb_composite_task();` add:

```c
        pump_emfi_cdc();
        emfi_campaign_tick();
```

Also extend `print_snapshot` to include the EMFI state — insert before
the closing of the `diag_printf(...)` call. Find the existing line:

```c
    diag_printf("ADC=%4u  SCAN=%s  TRIG=%d  CROWBAR=%s  HV[%s%s]\n",
                adc, bits, trigger ? 1 : 0, crowbar_label(path),
                armed   ? "ARM" : "---",
                charged ? " CHG" : "");
```

Replace with:

```c
    emfi_status_t es; emfi_campaign_get_status(&es);
    static const char *state_labels[] = {
        "IDLE","ARMING","CHARGED","WAITING","FIRED","ERROR"
    };
    const char *slabel = (es.state < 6) ? state_labels[es.state] : "???";
    diag_printf("ADC=%4u  SCAN=%s  TRIG=%d  CROWBAR=%s  HV[%s%s]  EMFI=%s\n",
                adc, bits, trigger ? 1 : 0, crowbar_label(path),
                armed   ? "ARM" : "---",
                charged ? " CHG" : "",
                slabel);
```

- [ ] **Step 3: Add `usb_composite_cdc_read` to usb_composite**

Edit `usb/include/usb_composite.h`, append:

```c
// Read up to `cap` bytes from CDC `idx` into `data`. Non-blocking;
// returns the count actually read (0 if nothing available).
size_t usb_composite_cdc_read(usb_cdc_index_t idx, void *data, size_t cap);
```

Edit `usb/src/usb_composite.c`, append at the bottom:

```c
size_t usb_composite_cdc_read(usb_cdc_index_t idx, void *data, size_t cap) {
    if ((unsigned)idx >= USB_CDC_COUNT || data == NULL || cap == 0) return 0;
    if (!tud_cdc_n_available((uint8_t)idx)) return 0;
    uint32_t n = tud_cdc_n_read((uint8_t)idx, data, (uint32_t)cap);
    return (size_t)n;
}
```

Edit `usb/src/usb_composite.c`'s existing `echo_cdc` loop — we still
want echo on CDC1/CDC2/CDC3 but NOT on CDC0 (that belongs to the
proto now). Replace the loop in `usb_composite_task`:

```c
    tud_task();
    // CDC0 is owned by emfi_proto (pumped from main.c); CDC1..CDC3 stay
    // in echo stub until later services claim them.
    for (uint8_t i = 1; i < USB_CDC_COUNT; i++) {
        echo_cdc(i);
    }
    pump_vendor();
```

- [ ] **Step 4: Stage**

```bash
git add apps/faultycat_fw/main.c apps/faultycat_fw/CMakeLists.txt \
        usb/include/usb_composite.h usb/src/usb_composite.c
```

- [ ] **Step 5: Build firmware**

```bash
cmake --build --preset fw-debug
```

Expected: UF2 built without error.

### Task 6.5: Write `tools/emfi_client.py`

**Files:**
- Create: `tools/emfi_client.py`

- [ ] **Step 1: Write minimal pyserial client**

```python
#!/usr/bin/env python3
"""FaultyCat v3 minimal EMFI host client (F4-6).

NOT the F10 Rust client. Reference tool only — pyserial round-trip
for configure/arm/fire/status/capture across CDC0.
"""
import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")

SOF = 0xFA
CMD = {
    "ping":      0x01,
    "configure": 0x10,
    "arm":       0x11,
    "fire":      0x12,
    "disarm":    0x13,
    "status":    0x14,
    "capture":   0x15,
}
TRIG = {
    "immediate":    0,
    "ext-rising":   1,
    "ext-falling":  2,
    "ext-pulse-pos":3,
}

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([cmd]) + struct.pack("<H", len(payload)) + payload
    crc = crc16_ccitt(body)
    return bytes([SOF]) + body + struct.pack("<H", crc)

def read_frame(ser, timeout=2.0) -> tuple[int, bytes]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = ser.read(1)
        if b and b[0] == SOF:
            header = ser.read(3)
            if len(header) != 3: continue
            cmd = header[0]
            length = struct.unpack("<H", header[1:3])[0]
            payload = ser.read(length)
            crc_bytes = ser.read(2)
            if len(payload) != length or len(crc_bytes) != 2: continue
            expected = struct.unpack("<H", crc_bytes)[0]
            calc = crc16_ccitt(header + payload)
            if expected != calc:
                continue
            return cmd, payload
    raise TimeoutError("no reply")

def cmd_ping(ser):
    ser.write(frame(CMD["ping"]))
    _, pl = read_frame(ser)
    print(f"PONG {pl!r}")

def cmd_configure(ser, trigger, delay, width, charge):
    pl = bytes([TRIG[trigger]]) + struct.pack("<III", delay, width, charge)
    ser.write(frame(CMD["configure"], pl))
    _, r = read_frame(ser)
    print(f"configure → err={r[0]}")

def cmd_arm(ser):
    ser.write(frame(CMD["arm"]))
    _, r = read_frame(ser); print(f"arm → err={r[0]}")

def cmd_fire(ser, trigger_timeout):
    ser.write(frame(CMD["fire"], struct.pack("<I", trigger_timeout)))
    _, r = read_frame(ser); print(f"fire → err={r[0]}")

def cmd_status(ser):
    ser.write(frame(CMD["status"]))
    _, r = read_frame(ser)
    state, err = r[0], r[1]
    last, fill, wus, dus = struct.unpack("<IIII", r[2:18])
    print(f"state={state} err={err} last={last}ms fill={fill} width={wus}us delay={dus}us")

def cmd_capture(ser, offset, length):
    ser.write(frame(CMD["capture"], struct.pack("<HH", offset, length)))
    _, r = read_frame(ser)
    print(f"capture[{offset}:{offset+length}] = {r.hex()}")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default="/dev/ttyACM0")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("ping")
    c = sub.add_parser("configure")
    c.add_argument("--trigger", choices=list(TRIG), required=True)
    c.add_argument("--delay", type=int, default=0)
    c.add_argument("--width", type=int, default=5)
    c.add_argument("--charge-timeout", type=int, default=1000)
    sub.add_parser("arm")
    f = sub.add_parser("fire"); f.add_argument("--trigger-timeout", type=int, default=1000)
    sub.add_parser("disarm")
    sub.add_parser("status")
    cp = sub.add_parser("capture")
    cp.add_argument("--offset", type=int, default=0)
    cp.add_argument("--length", type=int, default=64)
    args = p.parse_args()

    with serial.Serial(args.port, 115200, timeout=0.5) as ser:
        if args.cmd == "ping":       cmd_ping(ser)
        elif args.cmd == "configure":cmd_configure(ser, args.trigger, args.delay, args.width, args.charge_timeout)
        elif args.cmd == "arm":      cmd_arm(ser)
        elif args.cmd == "fire":     cmd_fire(ser, args.trigger_timeout)
        elif args.cmd == "disarm":   ser.write(frame(CMD["disarm"])); read_frame(ser); print("disarmed")
        elif args.cmd == "status":   cmd_status(ser)
        elif args.cmd == "capture":  cmd_capture(ser, args.offset, args.length)

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Make executable**

```bash
chmod +x tools/emfi_client.py
git add tools/emfi_client.py
```

### Task 6.6: Update docs

**Files:**
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/PORTING.md`
- Modify: `.claude/skills/faultycat-fase-actual/SKILL.md`

- [ ] **Step 1: Update ARCHITECTURE.md Status snapshot**

In the phase table, change the F4 row from `**next**` to `✓ closed`:

```
| F4 — glitch engine EMFI (service, PIO-driven triggered fire) | `v3.0-f4` | ✓ closed |
| F5 — glitch engine crowbar (service) | — | **next** |
```

Update the "Current tree health" list:

- Add `hal/pio ✓ F4-1` and `hal/dma ✓ F4-2` to the HAL bullet.
- Update the test count reflecting actuals (run ctest and paste the
  number — approximately 139 tests / 17 binaries).
- Change the version line above the table to `v3.0-f4 (2026-04-YY)`.

Insert a new paragraph after "Current tree health":

> **PIO instance allocation (frozen at F4-1):** `pio0` belongs to
> the glitch engines — SM0 is used by `services/glitch_engine/emfi/
> emfi_pio` for trigger+delay+pulse of GP14; F5 claims additional SMs
> on pio0 for the crowbar path. `pio1` is reserved for `swd_core`
> (F6), `target-uart` passthrough (F8), and `jtag_core` + scanner
> bit-banging (F8), splitting its 4 SMs across those consumers.

Update the Layers diagram. In the `services/` block change the EMFI
row from `… F4` to `✓ F4`:

```
│    glitch_engine/emfi/                │    EMFI campaign + PIO fire  ✓ F4    │
```

Add the host_proto row:

```
│    host_proto/emfi_proto              │    binary framing on CDC0    ✓ F4    │
```

In the HAL block, change:

```
│    pio    #error stub → F4       dma   #error stub → F4                     │
```

to:

```
│    pio    ✓ F4-1                 dma   ✓ F4-2                               │
```

In the USB composite section, update the CDC 0 row to show it's now
bound to the proto:

```
├── IAD + CDC 0 "EMFI Control"       IF 0 (notif) + IF 1 (data)  ✓ F4 emfi_proto
```

- [ ] **Step 2: Update PORTING.md**

Tick row 6:

```
| 6 | `firmware/c/trigger.pio`, `trigger_basic.pio`                 | Port, tidy         | `drivers/emfi_pulse/trigger.pio` → now `services/glitch_engine/emfi/emfi_pio.c` | [x] F4-3 — PIO trigger compiler reimplemented from scratch in emfi_pio.c (hand-authored opcodes). | Faultier-inspired arch (trigger block + delay + pulse linearised into one SM) but zero lines copied from unlicensed upstream. |
```

Rows 9, 10 stay `[x] not ported (policy)` but add a trailing clause
to the Rationale column: "— F4-3 service architecture mirrors the
compiler shape (trigger block → delay → glitch) without the code."

Add a new row after row 18:

```
| 19 | `firmware/c/glitcher/glitcher.c::prepare_adc`                 | Port (HW-proven)   | `services/glitch_engine/emfi/emfi_capture.c` | [x] F4-4 | FaultyCat-origin BSD-3 code. 8192-byte ring, 8-bit shift, DREQ_ADC preserved exactly. |
```

- [ ] **Step 3: Update the skill file**

Replace the "Fase actual" line at the top with:

```
## Fase actual: F4 cerrada → F5 a continuación (crowbar glitch_engine service)
```

In the "Tags cerrados" section append:

```
### `v3.0-f4` — EMFI glitch engine + host proto (2026-04-YY)
hal/pio + hal/dma lifted. services/glitch_engine/emfi/ complete:
emfi_pio (PIO trigger compiler, reimplemented from scratch),
emfi_capture (8 KB ADC DMA ring on GP29), emfi_campaign (state
machine + 100 ms HV invariant activated). services/host_proto/
emfi_proto on CDC0 with CRC16-CCITT framing. Button PULSE kept on
CPU fire path (F2b) for operator use. tools/emfi_client.py reference
host client. SAFETY.md §3 #5 activated in F4-5.
```

Rewrite the "En qué estamos ahora" block to describe F5 setup.
Minimal placeholder text is fine since F5 details live in the plan
document when it's authored:

```
## En qué estamos ahora — F5 (crowbar glitch_engine service)

Siguiente servicio: voltage glitching con crowbar MOSFET. Plan §6 F5.
Reusa el patrón de emfi_campaign: service compone drivers + PIO.
Safety gate cubre crowbar_mosfet si cambia break-before-make.

Pautas clave ya validadas en F4:
- Service owns PIO program build-from-scratch.
- 100 ms HV invariant pattern (si aplica a HV path; crowbar
  doesn't use HV per se pero sí drives peak current spikes).
- host_proto/* pattern con CRC16-CCITT framing replicable para
  crowbar_proto.

Entregables F5 pendientes — escribir plan detallado antes de tocar
código.
```

Change the "HAL" table to mark pio/dma as done:

```
| `hal/pio.h`  | ✓ | F4-1 |
| `hal/dma.h`  | ✓ | F4-2 |
```

Change the "Composite activo" block to mention emfi_proto on CDC0:

```
- 4 CDC: emfi(0) → emfi_proto binary, crowbar(1) echo (F5), scanner(2) diag, target-uart(3) echo.
```

- [ ] **Step 4: Stage all doc changes**

```bash
git add docs/ARCHITECTURE.md docs/PORTING.md .claude/skills/faultycat-fase-actual/SKILL.md
```

### Task 6.7: Final build + pre-flash verification

- [ ] **Step 1: Full green**

```bash
ctest --preset host-tests
cmake --build --preset fw-release
```

Expected: all tests green; release UF2 at `build/fw-release/apps/faultycat_fw/faultycat.uf2`.

### Task 6.8: **STOP** — physical checkpoint with Sabas

Per spec §10.2, run each checkpoint before the tag:

- [ ] **Checkpoint 1** — `lsusb -v` still shows 10 interfaces after flashing the F4 UF2. No regression on F3.
- [ ] **Checkpoint 2** — `openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk; init; shutdown"` still identifies the probe and returns a DAP_Info response.
- [ ] **Checkpoint 3** — `picocom /dev/ttyACM2` still shows the diag snapshot (now with `EMFI=IDLE`).
- [ ] **Checkpoint 4** — `tools/emfi_client.py --port /dev/ttyACM0 ping` returns `PONG b'F4\\x00\\x00'`.
- [ ] **Checkpoint 5** — With shield on and coil mounted:
  ```
  ./tools/emfi_client.py configure --trigger immediate --delay 0 --width 5 --charge-timeout 3000
  ./tools/emfi_client.py arm
  ./tools/emfi_client.py status    # expect state=2 (CHARGED) after a couple of seconds
  ./tools/emfi_client.py fire
  ./tools/emfi_client.py status    # expect state=4 (FIRED)
  ```
  Scope on SMA shows ~5 µs pulse.
- [ ] **Checkpoint 6** — Inject 1 kHz square wave on GP8, run `configure --trigger ext-rising --delay 100 --width 5`. Pulse follows each rising edge by 100 µs ±1 µs; scope jitter < 100 ns.
- [ ] **Checkpoint 7** — HV disconnected: `configure --charge-timeout 100` → `arm` → `status` shows `state=5 err=2` (ERROR(HV_NOT_CHARGED)) within 100–200 ms.
- [ ] **Checkpoint 8** — While service is WAITING (use a long trigger-timeout), press PULSE button. Diag line prints ignore message; no fire.
- [ ] **Checkpoint 9** — Disarm, verify PULSE button CPU-fire still works on next arm cycle.

Gate the commit on all 9 passing. If any fails, fix before tagging.

### Task 6.9: Commit F4-6 (SIGNED)

- [ ] **Step 1: Ask Sabas for filled checklist**

> "F4-6 ready. All 9 physical checkpoints pass (or list failures).
> Per SAFETY.md §1 SIGNED. Please fill:
>
> ```
> Safety: HV charger is in DISARMED state at firmware boot. [y]
> Safety: 60-second auto-disarm is active by default and tested. [y]
> Safety: plastic shield is installed for any physical test of this change. [y]
> Safety: operator has a known-good GND reference. [y]
> Safety: the output SMA is either loaded by an EMFI coil or discharged. [y]
> Safety: oscilloscope probe is rated for the measured voltage (10x / 100x / differential). [y]
> Safety: break-before-make on MOSFET gate transitions is preserved by this change. [NA]
> Safety-justification: this commit only integrates the EMFI path into CDC0; crowbar gate logic unchanged.
> Safety: build, flash, and verify were performed in person by the maintainer. [y]
> Signed-off-by: Sabas <sabasjimenez@gmail.com>
> ```"

- [ ] **Step 2: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(F4-6): emfi_proto on CDC0 + main.c integration — closes F4

services/host_proto/emfi_proto — CRC16-CCITT framing (SOF+CMD+LEN
+PAYLOAD+CRC). 7 commands: PING/CONFIGURE/ARM/FIRE/DISARM/STATUS/
CAPTURE. 100 ms inter-byte timeout resets parser. Reply CMD is
request CMD | 0x80.

apps/faultycat_fw/main.c wires the pump: CDC0 RX → emfi_proto_feed
→ emfi_proto_dispatch → CDC0 TX. emfi_campaign_tick() runs every
main loop (20 ms cadence). PULSE button stays on CPU fire path
(F2b); snapshot row on CDC2 now includes EMFI=<STATE>.

usb_composite gains cdc_read; CDC0 leaves echo mode and is owned
by emfi_proto. CDC1..CDC3 still echo until their service claims
them (F5 crowbar, F8 scanner, F8 target-uart).

tools/emfi_client.py — reference pyserial client. 11 new proto tests.

Docs tied off: ARCHITECTURE status snapshot to v3.0-f4, pio0/pio1
allocation paragraph, emfi_proto row in usb composite block.
PORTING rows 6 ticked, new row 19 for emfi_capture. SKILL.md
closes F4 and opens F5.

Physical checkpoints (§10.2 of the spec) all passed:
[List: 1-9 pass or detail failures + rerun]

<CHECKLIST_BODY>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Task 6.10: Tag `v3.0-f4`

- [ ] **Step 1: Tag annotated**

```bash
git tag -a v3.0-f4 -m "v3.0-f4 — EMFI glitch engine service

Sub-commits:
  F4-1 hal/pio
  F4-2 hal/dma
  F4-3 emfi_pio service + emfi_pulse PIO attach (SIGNED)
  F4-4 emfi_capture ADC DMA ring
  F4-5 emfi_campaign state machine + SAFETY invariant 5 (SIGNED)
  F4-6 emfi_proto on CDC0 + main.c integration (SIGNED)

Physical checkpoints per spec §10.2 all green. 75 → ~139 tests,
10 → 17 binaries. F3 invariants (lsusb, DAP_Info, diag) preserved."
```

- [ ] **Step 2: Verify tag**

```bash
git describe --tags
git log --oneline v3.0-f3..v3.0-f4
```

Expected: `v3.0-f4` printed. Log shows 6 commits (F4-1..F4-6).

### Task 6.11: Report to Sabas

- [ ] **Step 1: Compose closure message**

Brief summary of: commits landed, test count, checkpoints passed,
and what F5 brings next.

---

## Post-F4 self-review hooks

Before declaring "F4 done", confirm:

- [ ] `ctest --preset host-tests` — all green.
- [ ] `cmake --build --preset fw-release` — size diff vs `v3.0-f3` documented
      in the closure message (expect ~+8 KB for EMFI service + proto).
- [ ] `git log --grep Safety v3.0-f3..v3.0-f4` shows 3 signed commits
      (F4-3, F4-5, F4-6).
- [ ] `docs/ARCHITECTURE.md` grep for `v3.0-f4` confirms status
      snapshot was updated.
- [ ] `.claude/skills/faultycat-fase-actual/SKILL.md` mentions F5 as
      next and F4 as closed.

If any is red, open a follow-up commit before declaring closed.


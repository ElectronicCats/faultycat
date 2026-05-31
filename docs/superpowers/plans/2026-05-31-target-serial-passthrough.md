# Target Serial Passthrough Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a transparent host↔target serial bridge on CDC3, backed by a PIO UART (TX + RX) on `pio1/SM1+SM2`, controlled from the CDC2 shell and arbitrated by `swd_bus_lock`.

**Architecture:** New `services/target_serial/` with two files — `target_serial_pio.c` (thin `hal/pio` layer owning the two state machines + the UART PIO programs) and `target_serial.c` (state machine, baud→divider math, pin validation, bus-lock, byte primitives). `apps/faultycat_fw/main.c` gains a `pump_target_cdc()` that shovels bytes both ways between CDC3 and the byte primitives, plus a `serial` verb group in the shell. CDC3 stops echoing in `usb_composite.c`. The second RX state machine (`pio1/SM3`) is intentionally left free for the future passive sniffer (Increment 2).

**Tech Stack:** C11, RP2040 Pico-SDK, PIO, TinyUSB CDC, Unity host tests with `hal_fake`, CMake presets (`host-tests` / `rp2040`).

**Spec:** `docs/superpowers/specs/2026-05-31-target-serial-passthrough-design.md`

---

## File Structure

**Created:**
- `services/target_serial/target_serial_pio.h` — PIO layer API.
- `services/target_serial/target_serial_pio.c` — PIO programs + SM management + byte put/get.
- `services/target_serial/target_serial.h` — service API (state, config, primitives).
- `services/target_serial/target_serial.c` — state machine + baud math + bus-lock.
- `services/target_serial/CMakeLists.txt` — `service_target_serial` static lib.
- `tests/test_target_serial.c` — host unit tests for both modules via `hal_fake`.
- `docs/TARGET_SERIAL.md` — operator-facing doc (shell verbs + data path).

**Modified:**
- `services/swd_bus_lock/swd_bus_lock.h` — add `SWD_BUS_OWNER_SERIAL`.
- `tests/test_swd_bus_lock.c` — one test for the new owner.
- `services/CMakeLists.txt` — `add_subdirectory(target_serial)`.
- `tests/CMakeLists.txt` — test helper + `test_target_serial` registration.
- `apps/faultycat_fw/CMakeLists.txt` — link `service_target_serial`.
- `usb/src/usb_composite.c` — remove CDC3 echo (delete loop + `echo_cdc`).
- `apps/faultycat_fw/main.c` — include, `pump_target_cdc()`, `serial` shell verbs, init wiring, help text.
- `docs/ARCHITECTURE.md`, `docs/PORTING.md` — phase-close updates.

---

## Build / Test Commands (reference)

- Host tests (all): `bash scripts/run_tests.sh`
- Host tests (one): `cmake --preset host-tests >/dev/null && cmake --build build-host -j"$(nproc)" >/dev/null && ctest --test-dir build-host -R test_target_serial --output-on-failure`
- Firmware compile: `bash scripts/build_firmware.sh`

---

## Task 1: Add SERIAL owner to the SWD bus lock

**Files:**
- Modify: `services/swd_bus_lock/swd_bus_lock.h` (the `swd_bus_owner_t` enum)
- Test: `tests/test_swd_bus_lock.c`

- [ ] **Step 1: Write the failing test**

Add this function to `tests/test_swd_bus_lock.c` (place it just before the `int main(void)` that runs the Unity suite):

```c
void test_serial_owner_acquire_release(void) {
    swd_bus_lock_init();
    TEST_ASSERT_TRUE(swd_bus_acquire(SWD_BUS_OWNER_SERIAL, SWD_BUS_TIMEOUT_NONE));
    TEST_ASSERT_EQUAL_INT(SWD_BUS_OWNER_SERIAL, swd_bus_owner());
    swd_bus_release(SWD_BUS_OWNER_SERIAL);
    TEST_ASSERT_EQUAL_INT(SWD_BUS_OWNER_IDLE, swd_bus_owner());
}
```

And register it inside that file's `main()` next to the other `RUN_TEST(...)` lines:

```c
    RUN_TEST(test_serial_owner_acquire_release);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --preset host-tests >/dev/null && cmake --build build-host -j"$(nproc)" 2>&1 | tail -20`
Expected: COMPILE ERROR — `SWD_BUS_OWNER_SERIAL` undeclared.

- [ ] **Step 3: Add the enum value**

In `services/swd_bus_lock/swd_bus_lock.h`, extend the `swd_bus_owner_t` enum (currently ends at `SWD_BUS_OWNER_DAPLINK = 3`):

```c
typedef enum {
    SWD_BUS_OWNER_IDLE     = 0, // bus free
    SWD_BUS_OWNER_CAMPAIGN = 1, // glitch_engine post-fire SWD verify (F9)
    SWD_BUS_OWNER_SCANNER  = 2, // pinout_scanner during P(8,k) sweep (F8-2)
    SWD_BUS_OWNER_DAPLINK  = 3, // CMSIS-DAP from external host (F7)
    SWD_BUS_OWNER_SERIAL   = 4, // target_serial passthrough (Inc 1)
} swd_bus_owner_t;
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-host -j"$(nproc)" >/dev/null && ctest --test-dir build-host -R test_swd_bus_lock --output-on-failure`
Expected: PASS, all `test_swd_bus_lock` cases green.

- [ ] **Step 5: Commit**

```bash
git add services/swd_bus_lock/swd_bus_lock.h tests/test_swd_bus_lock.c
git commit -m "feat(swd_bus_lock): add SERIAL owner for target passthrough"
```

---

## Task 2: PIO UART layer + first tests

This task adds `target_serial_pio.{h,c}`, the CMake test helper, and the test file with the PIO-layer tests. The logic-layer tests are added in Task 3.

**Files:**
- Create: `services/target_serial/target_serial_pio.h`
- Create: `services/target_serial/target_serial_pio.c`
- Create: `services/target_serial/target_serial.h` (API only — needed so the test file compiles; impl lands in Task 3)
- Create: `services/target_serial/CMakeLists.txt`
- Create: `tests/test_target_serial.c`
- Modify: `services/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the PIO-layer header**

Create `services/target_serial/target_serial_pio.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/target_serial/target_serial_pio — PIO UART physical layer.
//
// Claims pio1/SM1 (UART TX) + pio1/SM2 (UART RX). pio1/SM0 is swd_phy;
// pio1/SM3 is deliberately left free for the Increment-2 passive
// sniffer's second RX. The two state machines run at 8x the UART baud
// (8 PIO cycles per bit), so the divider is sysclk / (baud * 8).
//
// Programs are the canonical raspberrypi/pico-examples UART programs
// (BSD-3), hand-encoded inline (this tree has no pioasm step). The RX
// program uses an explicit `push` instead of the upstream
// uart_rx_mini autopush, because hal_pio_sm_cfg_t does not expose the
// autopush threshold.

// Claim SM1+SM2, load both programs, route pins, configure dividers,
// enable. Returns false if a SM is already claimed, the PIO has no
// program room, or the instance is unavailable.
bool target_serial_pio_init(uint8_t tx_gp, uint8_t rx_gp, uint32_t divider);

// Stop + unclaim both SMs, remove both programs, restore the two pins
// to plain GPIO inputs so scanner_io / swd_phy can re-claim them.
// Safe to call when not inited.
void target_serial_pio_deinit(void);

// Reprogram the integer clock divider on both SMs (live baud change).
void target_serial_pio_set_divider(uint32_t divider);

// Non-blocking TX: queue one byte to the TX FIFO. Returns false if not
// inited or the FIFO is full.
bool target_serial_pio_try_put(uint8_t b);

// Non-blocking RX: pop one received byte. Returns false if not inited
// or the RX FIFO is empty.
bool target_serial_pio_try_get(uint8_t* out);
```

- [ ] **Step 2: Write the service header (API only)**

Create `services/target_serial/target_serial.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_v2.h"

// services/target_serial — transparent host<->target serial bridge
// (Increment 1). CDC3 carries raw bytes; this service owns the PIO
// UART and the swd_bus_lock acquisition. The byte-shoveling between
// CDC3 and these primitives lives in apps/faultycat_fw/main.c
// (pump_target_cdc) so this module stays free of TinyUSB and host-
// testable.

#define TARGET_SERIAL_TX_GP_DEFAULT BOARD_GP_SCANNER_CH4 // GP4
#define TARGET_SERIAL_RX_GP_DEFAULT BOARD_GP_SCANNER_CH5 // GP5
#define TARGET_SERIAL_BAUD_DEFAULT  115200u

typedef enum {
    TARGET_SERIAL_DISABLED = 0,
    TARGET_SERIAL_ENABLED  = 1,
} target_serial_state_t;

typedef struct {
    target_serial_state_t state;
    uint8_t tx_gp;
    uint8_t rx_gp;
    uint32_t baud;
} target_serial_status_t;

// sysclk / (baud * 8), rounded to nearest, clamped to 1..0xFFFF.
uint32_t target_serial_baud_to_divider(uint32_t baud);

// Reset to DISABLED with default pins/baud. No hardware touched.
void target_serial_init(void);

// Acquire the bus, configure + enable the PIO UART. Fails if already
// enabled, pins out of the scanner range (0..7) or equal, baud is 0,
// or the bus is held by another owner.
bool target_serial_enable(uint8_t tx_gp, uint8_t rx_gp, uint32_t baud);

// Live baud change. Returns false unless currently ENABLED and baud>0.
bool target_serial_set_baud(uint32_t baud);

// Disable + release the bus. No-op if already DISABLED.
void target_serial_disable(void);

void target_serial_get_status(target_serial_status_t* out);

// Byte primitives — no-ops returning false/0 while DISABLED.
bool target_serial_tx_byte(uint8_t b);
size_t target_serial_rx_drain(uint8_t* buf, size_t cap);
```

- [ ] **Step 3: Write the PIO-layer tests (failing)**

Create `tests/test_target_serial.c`:

```c
#include "unity.h"

#include "hal/pio.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"

#include "swd_bus_lock.h"
#include "target_serial.h"
#include "target_serial_pio.h"

#include <string.h>

// pio1 indices used by the service (mirrors target_serial_pio.c).
#define TS_INST 1
#define TS_TX_SM 1u
#define TS_RX_SM 2u

void setUp(void) {
    hal_fake_pio_reset();
    hal_fake_gpio_reset();
    swd_bus_lock_init();
    target_serial_init();
}

void tearDown(void) {
    target_serial_disable();
}

// --- PIO layer ---

void test_pio_init_claims_both_sms_and_loads_programs(void) {
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].claimed);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[TS_INST].sm[TS_RX_SM].claimed);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[TS_INST].program.loaded);
    target_serial_pio_deinit();
}

void test_pio_init_routes_pins(void) {
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[TS_INST].gpio_init_bitmap & (1u << 4u));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[TS_INST].gpio_init_bitmap & (1u << 5u));
    target_serial_pio_deinit();
}

void test_pio_deinit_unclaims_both_sms(void) {
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    target_serial_pio_deinit();
    TEST_ASSERT_FALSE(hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].claimed);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[TS_INST].sm[TS_RX_SM].claimed);
}

void test_pio_try_put_pushes_to_tx_fifo(void) {
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    TEST_ASSERT_TRUE(target_serial_pio_try_put(0xA5u));
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].tx_count);
    TEST_ASSERT_EQUAL_UINT32(0xA5u, hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].tx_fifo[0]);
    target_serial_pio_deinit();
}

void test_pio_try_get_reads_high_byte_of_word(void) {
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    // The RX program shifts right, so the received byte lands in
    // bits [31:24] of the FIFO word.
    hal_fake_pio_push_rx(TS_INST, TS_RX_SM, 0x41u << 24);
    uint8_t b = 0u;
    TEST_ASSERT_TRUE(target_serial_pio_try_get(&b));
    TEST_ASSERT_EQUAL_UINT8('A', b);
    target_serial_pio_deinit();
}

void test_pio_set_divider_updates_both_sms(void) {
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    target_serial_pio_set_divider(1628u);
    TEST_ASSERT_EQUAL_UINT32(1628u, hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].last_clkdiv_int);
    TEST_ASSERT_EQUAL_UINT32(1628u, hal_fake_pio_insts[TS_INST].sm[TS_RX_SM].last_clkdiv_int);
    target_serial_pio_deinit();
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pio_init_claims_both_sms_and_loads_programs);
    RUN_TEST(test_pio_init_routes_pins);
    RUN_TEST(test_pio_deinit_unclaims_both_sms);
    RUN_TEST(test_pio_try_put_pushes_to_tx_fifo);
    RUN_TEST(test_pio_try_get_reads_high_byte_of_word);
    RUN_TEST(test_pio_set_divider_updates_both_sms);
    return UNITY_END();
}
```

- [ ] **Step 4: Add the CMake plumbing**

Append to `services/CMakeLists.txt` (after `add_subdirectory(campaign_manager)`):

```cmake
add_subdirectory(target_serial)
```

Create `services/target_serial/CMakeLists.txt`:

```cmake
add_library(service_target_serial STATIC
    target_serial.c
    target_serial_pio.c
)

target_include_directories(service_target_serial
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_target_serial
    PUBLIC
        hal_rp2040
        board_v2_header
        service_swd_bus_lock
)
```

Add to `tests/CMakeLists.txt` (after the `faultycat_add_service_swd_test(test_swd_bus_lock)` block):

```cmake
# Inc 1 — target_serial passthrough (logic + PIO layer)
function(faultycat_add_service_target_serial_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/services/target_serial/target_serial.c
        ${CMAKE_SOURCE_DIR}/services/target_serial/target_serial_pio.c
        ${CMAKE_SOURCE_DIR}/services/swd_bus_lock/swd_bus_lock.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/services/target_serial
        ${CMAKE_SOURCE_DIR}/services/swd_bus_lock
        ${CMAKE_SOURCE_DIR}/Hardware/board_v2
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()
faultycat_add_service_target_serial_test(test_target_serial)
```

- [ ] **Step 5: Run test to verify it fails**

Run: `cmake --preset host-tests >/dev/null && cmake --build build-host -j"$(nproc)" 2>&1 | tail -20`
Expected: LINK ERROR — undefined references to `target_serial_pio_init` etc. (and `target_serial.c` empty / missing symbols). The compile of the test proves the headers and CMake are correct; the link fails because no `.c` bodies exist yet.

- [ ] **Step 6: Write the PIO-layer implementation**

Create `services/target_serial/target_serial_pio.c`:

```c
#include "target_serial_pio.h"

#include "hal/gpio.h"
#include "hal/pio.h"

// PIO UART programs derived from raspberrypi/pico-examples (BSD-3):
//   pio/uart_tx/uart_tx.pio  -> s_tx_prog (verbatim, 8 cycles/bit)
//   pio/uart_rx/uart_rx.pio  -> s_rx_prog (uart_rx_mini + explicit
//                               push, since hal_pio_sm_cfg_t exposes
//                               no autopush threshold)
// Both run the SM at 8x baud. Hand-encoded uint16_t (no pioasm step in
// this tree); jmp targets are 0-based within each program and relocated
// by hal_pio_add_program (== pico-sdk pio_add_program).

#define TS_TX_PROG_LEN 4u
static const uint16_t s_tx_prog[TS_TX_PROG_LEN] = {
    0x9fa0u, // 0: pull   block        side 1 [7]
    0xf727u, // 1: set    x, 7         side 0 [7]
    0x6001u, // 2: out    pins, 1                 (bitloop)
    0x0642u, // 3: jmp    x--, 2               [6]
};

#define TS_RX_PROG_LEN 6u
static const uint16_t s_rx_prog[TS_RX_PROG_LEN] = {
    0x2020u, // 0: wait   0 pin, 0
    0xea27u, // 1: set    x, 7                [10]
    0x4001u, // 2: in     pins, 1                 (bitloop)
    0x0642u, // 3: jmp    x--, 2               [6]
    0x8020u, // 4: push   block
    0x0000u, // 5: jmp    0
};

#define TS_TX_SM 1u // pio1/SM1 (SM0 is swd_phy)
#define TS_RX_SM 2u // pio1/SM2

static hal_pio_inst_t* s_pio = NULL;
static uint32_t s_tx_off     = 0u;
static uint32_t s_rx_off     = 0u;
static uint8_t s_tx_gp       = 0u;
static uint8_t s_rx_gp       = 0u;
static bool s_inited         = false;

bool target_serial_pio_init(uint8_t tx_gp, uint8_t rx_gp, uint32_t divider) {
    if (s_inited)
        return false;
    s_pio = hal_pio_instance(1); // pio1
    if (!s_pio)
        return false;
    if (!hal_pio_claim_sm(s_pio, TS_TX_SM))
        return false;
    if (!hal_pio_claim_sm(s_pio, TS_RX_SM)) {
        hal_pio_unclaim_sm(s_pio, TS_TX_SM);
        return false;
    }

    hal_pio_program_t txp = {.instructions = s_tx_prog, .length = TS_TX_PROG_LEN, .origin = -1};
    if (!hal_pio_add_program(s_pio, &txp, &s_tx_off)) {
        hal_pio_unclaim_sm(s_pio, TS_RX_SM);
        hal_pio_unclaim_sm(s_pio, TS_TX_SM);
        return false;
    }
    hal_pio_program_t rxp = {.instructions = s_rx_prog, .length = TS_RX_PROG_LEN, .origin = -1};
    if (!hal_pio_add_program(s_pio, &rxp, &s_rx_off)) {
        hal_pio_remove_program(s_pio, &txp, s_tx_off);
        hal_pio_unclaim_sm(s_pio, TS_RX_SM);
        hal_pio_unclaim_sm(s_pio, TS_TX_SM);
        return false;
    }

    // TX pin: PIO-owned output; preset HIGH below so idle = UART mark.
    hal_pio_gpio_init(s_pio, tx_gp);
    hal_pio_set_consecutive_pindirs(s_pio, TS_TX_SM, tx_gp, 1, true);
    // RX pin: PIO-owned input, pulled HIGH (UART idle).
    hal_pio_gpio_init(s_pio, rx_gp);
    hal_pio_set_consecutive_pindirs(s_pio, TS_RX_SM, rx_gp, 1, false);
    hal_gpio_set_pulls(rx_gp, true, false);

    hal_pio_sm_cfg_t txc = {
        .set_pin_base      = tx_gp,
        .set_pin_count     = 1,
        .out_pin_base      = tx_gp,
        .out_pin_count     = 1,
        .sideset_pin_base  = tx_gp,
        .sideset_pin_count = 1,
        .sideset_optional  = true,
        .sideset_pindirs   = false,
        .out_shift_right   = true, // UART is LSB-first
        .clk_div           = (float)divider,
    };
    hal_pio_sm_configure(s_pio, TS_TX_SM, s_tx_off, &txc);
    hal_pio_sm_clear_fifos(s_pio, TS_TX_SM);
    // Preset TX line HIGH (SET PINS,1 = 0xE001) so the line idles at a
    // mark instead of driving a spurious break before the first byte.
    hal_pio_sm_exec(s_pio, TS_TX_SM, 0xE001u);
    hal_pio_sm_set_enabled(s_pio, TS_TX_SM, true);

    hal_pio_sm_cfg_t rxc = {
        .in_pin_base    = rx_gp,
        .in_pin_count   = 1,
        .in_shift_right = true, // LSB-first; byte lands in word[31:24]
        .clk_div        = (float)divider,
    };
    hal_pio_sm_configure(s_pio, TS_RX_SM, s_rx_off, &rxc);
    hal_pio_sm_clear_fifos(s_pio, TS_RX_SM);
    hal_pio_sm_set_enabled(s_pio, TS_RX_SM, true);

    s_tx_gp = tx_gp;
    s_rx_gp = rx_gp;
    s_inited = true;
    return true;
}

void target_serial_pio_deinit(void) {
    if (!s_inited)
        return;
    hal_pio_sm_set_enabled(s_pio, TS_TX_SM, false);
    hal_pio_sm_set_enabled(s_pio, TS_RX_SM, false);

    hal_pio_program_t txp = {.instructions = s_tx_prog, .length = TS_TX_PROG_LEN, .origin = -1};
    hal_pio_program_t rxp = {.instructions = s_rx_prog, .length = TS_RX_PROG_LEN, .origin = -1};
    hal_pio_remove_program(s_pio, &rxp, s_rx_off);
    hal_pio_remove_program(s_pio, &txp, s_tx_off);
    hal_pio_unclaim_sm(s_pio, TS_RX_SM);
    hal_pio_unclaim_sm(s_pio, TS_TX_SM);

    // Restore pins so scanner_io / swd_phy can re-claim them.
    hal_gpio_init((hal_gpio_pin_t)s_tx_gp, HAL_GPIO_DIR_IN);
    hal_gpio_init((hal_gpio_pin_t)s_rx_gp, HAL_GPIO_DIR_IN);
    hal_gpio_set_pulls((hal_gpio_pin_t)s_rx_gp, false, false);

    s_pio    = NULL;
    s_inited = false;
}

void target_serial_pio_set_divider(uint32_t divider) {
    if (!s_inited)
        return;
    hal_pio_sm_set_clkdiv_int(s_pio, TS_TX_SM, divider);
    hal_pio_sm_set_clkdiv_int(s_pio, TS_RX_SM, divider);
}

bool target_serial_pio_try_put(uint8_t b) {
    if (!s_inited)
        return false;
    return hal_pio_sm_try_put(s_pio, TS_TX_SM, (uint32_t)b);
}

bool target_serial_pio_try_get(uint8_t* out) {
    if (!s_inited || out == NULL)
        return false;
    uint32_t w = 0u;
    if (!hal_pio_sm_try_get(s_pio, TS_RX_SM, &w))
        return false;
    *out = (uint8_t)(w >> 24); // RX shifts right → byte in high 8 bits
    return true;
}
```

> Note for the implementer: confirm `HAL_GPIO_DIR_IN` is the spelling used by `hal/gpio.h` (it is — see `swd_phy.c` `hal_gpio_init(..., HAL_GPIO_DIR_IN)`).

- [ ] **Step 7: Add a temporary stub for `target_serial.c` so the test links**

Create `services/target_serial/target_serial.c` with empty/no-op bodies for now (Task 3 fills them in). This lets Task 2's PIO tests link and pass without the logic layer:

```c
#include "target_serial.h"

#include "target_serial_pio.h"
#include "swd_bus_lock.h"

uint32_t target_serial_baud_to_divider(uint32_t baud) {
    (void)baud;
    return 1u;
}
void target_serial_init(void) {}
bool target_serial_enable(uint8_t tx_gp, uint8_t rx_gp, uint32_t baud) {
    (void)tx_gp;
    (void)rx_gp;
    (void)baud;
    return false;
}
bool target_serial_set_baud(uint32_t baud) {
    (void)baud;
    return false;
}
void target_serial_disable(void) {}
void target_serial_get_status(target_serial_status_t* out) {
    (void)out;
}
bool target_serial_tx_byte(uint8_t b) {
    (void)b;
    return false;
}
size_t target_serial_rx_drain(uint8_t* buf, size_t cap) {
    (void)buf;
    (void)cap;
    return 0u;
}
```

- [ ] **Step 8: Run test to verify it passes**

Run: `cmake --build build-host -j"$(nproc)" >/dev/null && ctest --test-dir build-host -R test_target_serial --output-on-failure`
Expected: PASS, 6 PIO-layer cases green.

- [ ] **Step 9: Commit**

```bash
git add services/target_serial/ services/CMakeLists.txt tests/CMakeLists.txt tests/test_target_serial.c
git commit -m "feat(target_serial): PIO UART TX+RX layer (pio1/SM1+SM2)"
```

---

## Task 3: target_serial logic layer (state machine, baud, bus-lock)

Replace the Task-2 stub `target_serial.c` with the real implementation, driven test-first by adding logic-layer tests to `tests/test_target_serial.c`.

**Files:**
- Modify: `tests/test_target_serial.c` (add tests + register them)
- Modify: `services/target_serial/target_serial.c` (replace stub with real impl)

- [ ] **Step 1: Add the failing logic-layer tests**

In `tests/test_target_serial.c`, add these functions before `int main(void)`:

```c
// --- baud math ---

void test_baud_to_divider_115200(void) {
    // 125e6 / (115200*8) = 135.63 -> 136
    TEST_ASSERT_EQUAL_UINT32(136u, target_serial_baud_to_divider(115200u));
}

void test_baud_to_divider_9600(void) {
    // 125e6 / (9600*8) = 1627.6 -> 1628
    TEST_ASSERT_EQUAL_UINT32(1628u, target_serial_baud_to_divider(9600u));
}

// --- state machine ---

void test_init_is_disabled(void) {
    target_serial_status_t st;
    target_serial_get_status(&st);
    TEST_ASSERT_EQUAL_INT(TARGET_SERIAL_DISABLED, st.state);
}

void test_enable_claims_sms_and_acquires_lock(void) {
    TEST_ASSERT_TRUE(target_serial_enable(4u, 5u, 115200u));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].claimed);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[TS_INST].sm[TS_RX_SM].claimed);
    TEST_ASSERT_EQUAL_INT(SWD_BUS_OWNER_SERIAL, swd_bus_owner());
    target_serial_status_t st;
    target_serial_get_status(&st);
    TEST_ASSERT_EQUAL_INT(TARGET_SERIAL_ENABLED, st.state);
    TEST_ASSERT_EQUAL_UINT8(4u, st.tx_gp);
    TEST_ASSERT_EQUAL_UINT8(5u, st.rx_gp);
    TEST_ASSERT_EQUAL_UINT32(115200u, st.baud);
}

void test_enable_rejects_out_of_range_pins(void) {
    TEST_ASSERT_FALSE(target_serial_enable(8u, 5u, 115200u));
    TEST_ASSERT_FALSE(target_serial_enable(4u, 9u, 115200u));
    TEST_ASSERT_EQUAL_INT(SWD_BUS_OWNER_IDLE, swd_bus_owner());
}

void test_enable_rejects_same_pin(void) {
    TEST_ASSERT_FALSE(target_serial_enable(4u, 4u, 115200u));
}

void test_enable_rejects_zero_baud(void) {
    TEST_ASSERT_FALSE(target_serial_enable(4u, 5u, 0u));
}

void test_double_enable_fails(void) {
    TEST_ASSERT_TRUE(target_serial_enable(4u, 5u, 115200u));
    TEST_ASSERT_FALSE(target_serial_enable(4u, 5u, 115200u));
}

void test_enable_fails_when_bus_held(void) {
    TEST_ASSERT_TRUE(swd_bus_acquire(SWD_BUS_OWNER_SCANNER, SWD_BUS_TIMEOUT_NONE));
    TEST_ASSERT_FALSE(target_serial_enable(4u, 5u, 115200u));
    swd_bus_release(SWD_BUS_OWNER_SCANNER);
}

void test_disable_releases_lock_and_sms(void) {
    TEST_ASSERT_TRUE(target_serial_enable(4u, 5u, 115200u));
    target_serial_disable();
    TEST_ASSERT_EQUAL_INT(SWD_BUS_OWNER_IDLE, swd_bus_owner());
    TEST_ASSERT_FALSE(hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].claimed);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[TS_INST].sm[TS_RX_SM].claimed);
}

// --- live baud ---

void test_set_baud_before_enable_is_noop(void) {
    TEST_ASSERT_FALSE(target_serial_set_baud(9600u));
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].set_clkdiv_int_calls);
}

void test_set_baud_reprograms_both_dividers(void) {
    TEST_ASSERT_TRUE(target_serial_enable(4u, 5u, 115200u));
    TEST_ASSERT_TRUE(target_serial_set_baud(9600u));
    TEST_ASSERT_EQUAL_UINT32(1628u, hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].last_clkdiv_int);
    TEST_ASSERT_EQUAL_UINT32(1628u, hal_fake_pio_insts[TS_INST].sm[TS_RX_SM].last_clkdiv_int);
    target_serial_status_t st;
    target_serial_get_status(&st);
    TEST_ASSERT_EQUAL_UINT32(9600u, st.baud);
}

// --- byte primitives ---

void test_tx_byte_when_disabled_returns_false(void) {
    TEST_ASSERT_FALSE(target_serial_tx_byte(0x55u));
}

void test_tx_byte_puts_to_tx_sm(void) {
    TEST_ASSERT_TRUE(target_serial_enable(4u, 5u, 115200u));
    TEST_ASSERT_TRUE(target_serial_tx_byte(0xA5u));
    TEST_ASSERT_EQUAL_UINT32(0xA5u, hal_fake_pio_insts[TS_INST].sm[TS_TX_SM].tx_fifo[0]);
}

void test_rx_drain_returns_pushed_bytes(void) {
    TEST_ASSERT_TRUE(target_serial_enable(4u, 5u, 115200u));
    hal_fake_pio_push_rx(TS_INST, TS_RX_SM, 0x41u << 24);
    hal_fake_pio_push_rx(TS_INST, TS_RX_SM, 0x42u << 24);
    uint8_t buf[8];
    size_t n = target_serial_rx_drain(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(2u, n);
    TEST_ASSERT_EQUAL_UINT8('A', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('B', buf[1]);
}

void test_rx_drain_bounded_by_capacity(void) {
    TEST_ASSERT_TRUE(target_serial_enable(4u, 5u, 115200u));
    for (int i = 0; i < 5; i++)
        hal_fake_pio_push_rx(TS_INST, TS_RX_SM, (uint32_t)(0x30 + i) << 24);
    uint8_t buf[3];
    size_t n = target_serial_rx_drain(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT(3u, n);
}
```

Register them in `main()` (add after the existing `RUN_TEST` lines, before `return UNITY_END();`):

```c
    RUN_TEST(test_baud_to_divider_115200);
    RUN_TEST(test_baud_to_divider_9600);
    RUN_TEST(test_init_is_disabled);
    RUN_TEST(test_enable_claims_sms_and_acquires_lock);
    RUN_TEST(test_enable_rejects_out_of_range_pins);
    RUN_TEST(test_enable_rejects_same_pin);
    RUN_TEST(test_enable_rejects_zero_baud);
    RUN_TEST(test_double_enable_fails);
    RUN_TEST(test_enable_fails_when_bus_held);
    RUN_TEST(test_disable_releases_lock_and_sms);
    RUN_TEST(test_set_baud_before_enable_is_noop);
    RUN_TEST(test_set_baud_reprograms_both_dividers);
    RUN_TEST(test_tx_byte_when_disabled_returns_false);
    RUN_TEST(test_tx_byte_puts_to_tx_sm);
    RUN_TEST(test_rx_drain_returns_pushed_bytes);
    RUN_TEST(test_rx_drain_bounded_by_capacity);
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build-host -j"$(nproc)" >/dev/null && ctest --test-dir build-host -R test_target_serial --output-on-failure`
Expected: FAIL — the stub returns `false`/`1`, so e.g. `test_enable_claims_sms_and_acquires_lock` and `test_baud_to_divider_115200` fail.

- [ ] **Step 3: Replace the stub with the real implementation**

Overwrite `services/target_serial/target_serial.c`:

```c
#include "target_serial.h"

#include "target_serial_pio.h"
#include "swd_bus_lock.h"

// RP2040 default boot clock; the PIO UART programs run at 8 cycles per
// bit, so SM clock target = baud * 8. Matches the sysclk assumption in
// swd_phy.c (SWD_SYSCLK_KHZ = 125000).
#define TS_SYSCLK_HZ      125000000u
#define TS_CYCLES_PER_BIT 8u

static target_serial_state_t s_state = TARGET_SERIAL_DISABLED;
static uint8_t s_tx_gp               = TARGET_SERIAL_TX_GP_DEFAULT;
static uint8_t s_rx_gp               = TARGET_SERIAL_RX_GP_DEFAULT;
static uint32_t s_baud               = TARGET_SERIAL_BAUD_DEFAULT;

uint32_t target_serial_baud_to_divider(uint32_t baud) {
    if (baud == 0u)
        return 0xFFFFu;
    uint32_t denom = baud * TS_CYCLES_PER_BIT;
    uint32_t div   = (TS_SYSCLK_HZ + denom / 2u) / denom; // round to nearest
    if (div == 0u)
        div = 1u;
    if (div > 0xFFFFu)
        div = 0xFFFFu;
    return div;
}

void target_serial_init(void) {
    s_state = TARGET_SERIAL_DISABLED;
    s_tx_gp = TARGET_SERIAL_TX_GP_DEFAULT;
    s_rx_gp = TARGET_SERIAL_RX_GP_DEFAULT;
    s_baud  = TARGET_SERIAL_BAUD_DEFAULT;
}

bool target_serial_enable(uint8_t tx_gp, uint8_t rx_gp, uint32_t baud) {
    if (s_state == TARGET_SERIAL_ENABLED)
        return false;
    if (tx_gp >= BOARD_GP_SCANNER_COUNT || rx_gp >= BOARD_GP_SCANNER_COUNT)
        return false;
    if (tx_gp == rx_gp)
        return false;
    if (baud == 0u)
        return false;
    if (!swd_bus_acquire(SWD_BUS_OWNER_SERIAL, SWD_BUS_TIMEOUT_NONE))
        return false;
    if (!target_serial_pio_init(tx_gp, rx_gp, target_serial_baud_to_divider(baud))) {
        swd_bus_release(SWD_BUS_OWNER_SERIAL);
        return false;
    }
    s_tx_gp = tx_gp;
    s_rx_gp = rx_gp;
    s_baud  = baud;
    s_state = TARGET_SERIAL_ENABLED;
    return true;
}

bool target_serial_set_baud(uint32_t baud) {
    if (s_state != TARGET_SERIAL_ENABLED || baud == 0u)
        return false;
    target_serial_pio_set_divider(target_serial_baud_to_divider(baud));
    s_baud = baud;
    return true;
}

void target_serial_disable(void) {
    if (s_state != TARGET_SERIAL_ENABLED)
        return;
    target_serial_pio_deinit();
    swd_bus_release(SWD_BUS_OWNER_SERIAL);
    s_state = TARGET_SERIAL_DISABLED;
}

void target_serial_get_status(target_serial_status_t* out) {
    if (out == NULL)
        return;
    out->state = s_state;
    out->tx_gp = s_tx_gp;
    out->rx_gp = s_rx_gp;
    out->baud  = s_baud;
}

bool target_serial_tx_byte(uint8_t b) {
    if (s_state != TARGET_SERIAL_ENABLED)
        return false;
    return target_serial_pio_try_put(b);
}

size_t target_serial_rx_drain(uint8_t* buf, size_t cap) {
    if (s_state != TARGET_SERIAL_ENABLED || buf == NULL || cap == 0u)
        return 0u;
    // Bounded by `cap` (caller passes a fixed-size stack buffer), so a
    // flood on the RX line can never spin this unbounded and starve
    // tud_task() / the magic-baud BOOTSEL path.
    size_t n = 0u;
    while (n < cap) {
        uint8_t b;
        if (!target_serial_pio_try_get(&b))
            break;
        buf[n++] = b;
    }
    return n;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build-host -j"$(nproc)" >/dev/null && ctest --test-dir build-host -R test_target_serial --output-on-failure`
Expected: PASS — all 22 cases (6 PIO + 16 logic) green.

- [ ] **Step 5: Run the full host suite (no regressions)**

Run: `bash scripts/run_tests.sh`
Expected: 100% tests passed (includes `test_swd_bus_lock` and `test_target_serial`).

- [ ] **Step 6: Commit**

```bash
git add services/target_serial/target_serial.c tests/test_target_serial.c
git commit -m "feat(target_serial): logic layer — state, baud math, bus-lock"
```

---

## Task 4: Stop CDC3 echo in the USB composite

**Files:**
- Modify: `usb/src/usb_composite.c`

- [ ] **Step 1: Remove the echo loop and the now-unused helper**

In `usb/src/usb_composite.c`, inside `usb_composite_task()`, delete the CDC3 echo loop:

```c
    for (uint8_t i = 3; i < USB_CDC_COUNT; i++) {
        echo_cdc(i);
    }
```

Replace it with a comment:

```c
    // CDC3 (target-uart) is no longer echoed: apps/faultycat_fw/main.c
    // owns it via pump_target_cdc() (transparent serial passthrough,
    // Inc 1). Reading it here would race that pump and steal bytes
    // (the F5-4 / F6-5 echo bug — see memory feedback_usb_cdc_echo_loop).
```

Then delete the now-unused `echo_cdc` function definition (the whole `static void echo_cdc(uint8_t cdc_idx) { ... }` block near the top) — leaving it would trip the `-Wunused-function` build.

- [ ] **Step 2: Verify the firmware still compiles**

Run: `bash scripts/build_firmware.sh 2>&1 | tail -20`
Expected: builds clean to `.uf2` (no `unused-function` warning/error for `echo_cdc`).

> If `echo_cdc` is referenced anywhere else, the build will fail at the call site — grep `echo_cdc` first to confirm it is only the deleted loop (it is, per the codebase survey).

- [ ] **Step 3: Commit**

```bash
git add usb/src/usb_composite.c
git commit -m "refactor(usb): stop echoing CDC3 — owned by serial passthrough pump"
```

---

## Task 5: Wire the passthrough into main.c (pump + shell + init + help)

**Files:**
- Modify: `apps/faultycat_fw/main.c`
- Modify: `apps/faultycat_fw/CMakeLists.txt`

- [ ] **Step 1: Link the service into the firmware target**

In `apps/faultycat_fw/CMakeLists.txt`, add `service_target_serial` to the `target_link_libraries(faultycat PRIVATE ...)` list (e.g. right after `service_swd_bus_lock`):

```cmake
    service_swd_bus_lock
    service_target_serial
    service_campaign_manager
```

- [ ] **Step 2: Include the service header**

In `apps/faultycat_fw/main.c`, add to the include block near the other service headers (around the `#include "swd_bus_lock.h"` line):

```c
#include "target_serial.h"
```

- [ ] **Step 3: Add the CDC3 pump**

In `apps/faultycat_fw/main.c`, add this function immediately after `pump_crowbar_cdc()` (just before the `main` section banner around line 1285). Stack buffers are fine here — `pump_target_cdc` is only called from the top-level main loop, never from the deep nested campaign yield-pumps, so the `static`-buffer rule (feedback_pump_reply_static) does not apply:

```c
// Pump CDC3 (target-uart) ↔ the PIO UART bridge. Transparent: raw
// bytes both ways, no framing. No-op while the bridge is DISABLED
// (target_serial_tx_byte / _rx_drain return false / 0). Called from
// the top-level main loop only, so 64 B stack buffers are safe.
static void pump_target_cdc(void) {
    // Host → target.
    uint8_t in[64];
    size_t n = usb_composite_cdc_read(USB_CDC_TARGET, in, sizeof(in));
    for (size_t i = 0; i < n; i++) {
        // Drop on TX-FIFO-full: a transparent bridge mirrors a real
        // USB-serial adapter; backpressure is the host's problem.
        (void)target_serial_tx_byte(in[i]);
    }
    // Target → host (drain bounded by the buffer size).
    uint8_t out[64];
    size_t m = target_serial_rx_drain(out, sizeof(out));
    if (m > 0) {
        usb_composite_cdc_write(USB_CDC_TARGET, out, m);
    }
}
```

- [ ] **Step 4: Add the `serial` shell verb handlers**

In `apps/faultycat_fw/main.c`, add these handlers just above `static void process_shell_line(char* line)` (around line 1045):

```c
// -----------------------------------------------------------------------------
// `serial` verbs — target-UART passthrough control (Inc 1). Data flows
// on CDC3; only configuration/enable goes through this CDC2 shell.
// -----------------------------------------------------------------------------

static void cmd_serial_init(int argc, char** argv) {
    uint8_t tx = (argc >= 3) ? (uint8_t)strtoul(argv[2], NULL, 0) : TARGET_SERIAL_TX_GP_DEFAULT;
    uint8_t rx = (argc >= 4) ? (uint8_t)strtoul(argv[3], NULL, 0) : TARGET_SERIAL_RX_GP_DEFAULT;
    uint32_t baud = (argc >= 5) ? (uint32_t)strtoul(argv[4], NULL, 0) : TARGET_SERIAL_BAUD_DEFAULT;
    if (target_serial_enable(tx, rx, baud)) {
        shell_printf("SERIAL: OK tx=GP%u rx=GP%u baud=%lu\n", tx, rx, (unsigned long)baud);
    } else {
        shell_print("SERIAL: ERR enable_failed (bus busy / pins not 0..7 / same pin / "
                    "already enabled)\n");
    }
}

static void cmd_serial_baud(int argc, char** argv) {
    if (argc < 3) {
        shell_print("SERIAL: ERR baud needs <n>\n");
        return;
    }
    uint32_t baud = (uint32_t)strtoul(argv[2], NULL, 0);
    if (target_serial_set_baud(baud)) {
        shell_printf("SERIAL: OK baud=%lu\n", (unsigned long)baud);
    } else {
        shell_print("SERIAL: ERR not_enabled_or_bad_baud\n");
    }
}

static void cmd_serial_status(void) {
    target_serial_status_t st;
    target_serial_get_status(&st);
    shell_printf("SERIAL: state=%s tx=GP%u rx=GP%u baud=%lu\n",
                 st.state == TARGET_SERIAL_ENABLED ? "ENABLED" : "DISABLED", st.tx_gp, st.rx_gp,
                 (unsigned long)st.baud);
}

static void process_serial_subcmd(int argc, char** argv) {
    if (argc < 2) {
        shell_print("SERIAL: ERR serial needs subcommand: init|baud|deinit|status\n");
        return;
    }
    const char* sub = argv[1];
    if (!strcmp(sub, "init")) {
        cmd_serial_init(argc, argv);
    } else if (!strcmp(sub, "baud")) {
        cmd_serial_baud(argc, argv);
    } else if (!strcmp(sub, "deinit")) {
        target_serial_disable();
        shell_print("SERIAL: OK deinit\n");
    } else if (!strcmp(sub, "status")) {
        cmd_serial_status();
    } else {
        shell_printf("SERIAL: ERR unknown_subcmd: %s\n", sub);
    }
}
```

- [ ] **Step 5: Dispatch the `serial` verb**

In `process_shell_line()`, add this block after the `serprog` dispatch (after the `if (!strcmp(argv[0], "serprog")) { ... }` block, before the `jtag` WIP block):

```c
    if (!strcmp(argv[0], "serial")) {
        process_serial_subcmd(argc, argv);
        return;
    }
```

- [ ] **Step 6: Initialize the service and call the pump in the main loop**

In `main()`, add the init right after `swd_bus_lock_init();` (around line 1318):

```c
    swd_bus_lock_init();
    target_serial_init();
```

And in the main `while (true)` loop, add the pump right after `pump_shell_cdc();` (around line 1332):

```c
        pump_shell_cdc();
        pump_target_cdc();
```

- [ ] **Step 7: Add the help text**

In `shell_help()`, add a `serial` section (insert after the `campaign` block, before the `--- Mode switches ---` line):

```c
    shell_print("SHELL: --- Target UART passthrough (Inc 1) ---\n");
    shell_print("SHELL:   serial init [<tx_gp> <rx_gp> <baud>]         defaults: 4 5 115200\n");
    shell_print("SHELL:   serial baud <n>                              live baud change\n");
    shell_print("SHELL:   serial deinit                                release bus + pins\n");
    shell_print("SHELL:   serial status                                show state/pins/baud\n");
    shell_print("SHELL:   (data flows on CDC3 — open it as a normal serial port)\n");
```

- [ ] **Step 8: Verify the firmware compiles**

Run: `bash scripts/build_firmware.sh 2>&1 | tail -20`
Expected: builds clean to `.uf2`, no warnings about unused statics (`cmd_serial_*`, `process_serial_subcmd`, `pump_target_cdc` are all referenced).

- [ ] **Step 9: Run the full host suite (no regressions)**

Run: `bash scripts/run_tests.sh`
Expected: 100% tests passed.

- [ ] **Step 10: Commit**

```bash
git add apps/faultycat_fw/main.c apps/faultycat_fw/CMakeLists.txt
git commit -m "feat(fw): wire target-serial passthrough — pump, shell verbs, init"
```

---

## Task 6: Documentation

**Files:**
- Create: `docs/TARGET_SERIAL.md`
- Modify: `docs/ARCHITECTURE.md`
- Modify: `docs/PORTING.md`

- [ ] **Step 1: Write the operator doc**

Create `docs/TARGET_SERIAL.md`:

```markdown
# Target Serial Passthrough (Increment 1)

A transparent host↔target UART bridge. CDC3 ("FaultyCat Target UART")
carries raw bytes; open it with any terminal (`minicom`, `screen`,
`pyserial`) and you talk to the target as if wired directly.

## Wiring

Uses the scanner header (GP0–GP7, level-shifted via TXS0108). Defaults:

| Signal | Pin |
|--------|-----|
| TX (FaultyCat → target RX) | GP4 |
| RX (target TX → FaultyCat) | GP5 |

8N1, default 115200 baud. Pins are runtime-selectable.

## Control (CDC2 scanner shell)

| Command | Effect |
|---------|--------|
| `serial init [tx_gp] [rx_gp] [baud]` | Acquire the bus, enable the bridge. Defaults `4 5 115200`. |
| `serial baud <n>` | Change baud live. |
| `serial deinit` | Disable, release the bus + pins. |
| `serial status` | Show state / pins / baud. |

`serial init` fails if the SWD/JTAG/scan bus is busy (it shares the
scanner header via `swd_bus_lock`), if pins are outside 0..7 or equal,
or if already enabled. Run `serial deinit` before a `scan swd`.

## Data path

CDC3 ⇄ `pump_target_cdc()` ⇄ PIO UART (`pio1/SM1` TX, `pio1/SM2` RX).
No framing — bytes pass through untouched. `pio1/SM3` is reserved for
the Increment-2 passive sniffer (second RX).

## Notes

- The 1200-baud BOOTSEL escape still works on every CDC, including CDC3.
- During a campaign or pinout scan the bus lock is held elsewhere, so
  the bridge must be `deinit`'d first.
```

- [ ] **Step 2: Update ARCHITECTURE.md**

In `docs/ARCHITECTURE.md`:
- Change the CDC3 line `IAD + CDC 3 "Target UART"  IF 6 (notif) + IF 7 (data)  → PIO UART passthru` to mark it implemented, e.g. append `✓ Inc1 target_serial (pio1/SM1+SM2)`.
- Where the PIO allocation is described (the `target-uart (F8) and the eventual buspirate-compat SPI bit-banger` paragraph), record that `pio1/SM1` = UART TX and `pio1/SM2` = UART RX are now taken by `services/target_serial`, and `pio1/SM3` is reserved for the Increment-2 sniffer.

- [ ] **Step 3: Update PORTING.md**

Add a short entry noting the new `services/target_serial/` module: `target_serial_pio.c` (PIO UART TX/RX on pio1/SM1+SM2, programs from pico-examples BSD-3) and `target_serial.c` (state + baud + bus-lock), controlled via the CDC2 `serial` verbs, data on CDC3.

- [ ] **Step 4: Commit**

```bash
git add docs/TARGET_SERIAL.md docs/ARCHITECTURE.md docs/PORTING.md
git commit -m "docs: target-serial passthrough — operator + architecture notes"
```

---

## Task 7: Physical smoke (maintainer gate — before any tag)

Per the maintainer rules (`feedback_smoke_before_tag`, `feedback_ext_trigger_needs_jumper`): host tests passing is necessary but NOT sufficient — the PIO UART timing (hand-encoded programs, divider rounding) can only be validated on hardware.

- [ ] **Step 1: Flash and run the bridge**

```bash
bash scripts/build_firmware.sh
bash scripts/flash.sh   # or cp build/apps/faultycat_fw/faultycat.uf2 to RPI-RP2
```

- [ ] **Step 2: Loopback test**

- Jumper GP4↔GP5 on the scanner header.
- Open the CDC2 scanner shell; run `serial init 4 5 115200`.
- Open CDC3 (target UART) in a terminal; type — each char should echo back (TX wraps to RX through the jumper).
- `serial baud 9600`, repeat. Confirm characters are still clean (divider rounding OK at both bauds).

- [ ] **Step 3: Real target echo**

- Remove the jumper; wire GP4→target RX, GP5→target TX, GND→GND with a USB-TTL adapter on the host running a known echo.
- Confirm bidirectional traffic at 115200.

- [ ] **Step 4: Regression + bus-lock interaction**

- With the bridge enabled, run `scan swd` → expect the scan to refuse or the operator to `serial deinit` first; confirm `serial init` after a `scan swd` session works (bus released cleanly).
- Confirm the 1200-baud BOOTSEL escape still drops to mass-storage on CDC3.
- Run the golden + regression checklist; report the result table.

- [ ] **Step 5: Report and wait**

Post the smoke result table. Do NOT tag `v3.0-fN` — wait for the maintainer. Surface any empirical fixes as `F(N)-polish` commits before the tag.

---

## Self-Review

**Spec coverage:**
- Transparent CDC3 bridge, no framing → Task 4 (echo removal) + Task 5 (`pump_target_cdc`). ✓
- Control via CDC2 shell (baud/pins/on-off) → Task 5 `serial` verbs. ✓
- 8N1, default 115200, default GP4/GP5, scanner header, bus-lock → Tasks 2/3 (`target_serial.h` defaults, `target_serial_enable` validation + lock). ✓
- One PIO UART now (TX SM1 + RX SM2), SM3 left free for sniffer → Task 2 (`target_serial_pio.c` claims SM1/SM2 only). ✓
- Echo-loop fix → Task 4. ✓
- `tud_cdc_line_coding_cb` untouched (BOOTSEL safe) → not modified by any task; verified in Task 7 Step 4. ✓
- Bounded RX drain → Task 3 `target_serial_rx_drain` bounded by `cap`. ✓
- Static-buffer rule N/A (top-level pump) → documented in Task 5 Step 3. ✓
- Host tests + physical smoke → Tasks 2/3 + Task 7. ✓
- Docs at phase close → Task 6. ✓
- Non-goals (2nd RX, framing, timestamps, auto-baud, parity) → none implemented. ✓

**Placeholder scan:** No TBD/TODO; every code/test/CMake step shows full content. ✓

**Type consistency:** `target_serial_*` signatures identical across header (Task 2), stub (Task 2), impl (Task 3), and call sites (Task 5). PIO indices `TS_TX_SM=1`, `TS_RX_SM=2` consistent between `target_serial_pio.c` and `test_target_serial.c` (`TS_TX_SM`/`TS_RX_SM`). Divider expectations (136 @115200, 1628 @9600) match `target_serial_baud_to_divider`. `SWD_BUS_OWNER_SERIAL` defined in Task 1, used in Tasks 3. ✓

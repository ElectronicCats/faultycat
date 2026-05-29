// Unit tests for services/swd_core/swd_dp — drives swd_phy through
// the hal_fake_pio FIFO and simulates target ACK/data responses by
// pre-populating the RX FIFO.

#include "unity.h"

#include "board_v2.h"
#include "hal/pio.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "swd_dp.h"
#include "swd_phy.h"

#define PIO1 1u
#define SM0  0u

// swd_phy_write_bits XOR-inverts the data word before pushing to the
// PIO FIFO so the bitloop's `out pindirs, 1` produces the push-pull
// wire pattern callers expect (open-drain emulation needed because
// the TXS0108E level shifter on FaultyCat v2.x scanner header
// breaks bidirectional push-pull SWD). Tests that inspect raw FIFO
// content must therefore compare against the inverted value.
#define OD_INV8(x)  ((uint8_t) ~(uint8_t)(x))
#define OD_INV32(x) (~(uint32_t)(x))

static const uint8_t SWCLK = BOARD_GP_SCANNER_CH0;
static const uint8_t SWDIO = BOARD_GP_SCANNER_CH1;

void setUp(void) {
    hal_fake_pio_reset();
    hal_fake_gpio_reset();
    swd_phy_init(SWCLK, SWDIO, SWD_PHY_NRST_NONE);
    // Reset TX count after init's bootstrap traffic so per-test
    // assertions count from zero.
    hal_fake_pio_insts[PIO1].sm[SM0].tx_count = 0;
}

void tearDown(void) {
    swd_phy_deinit();
}

// -----------------------------------------------------------------------------
// RX FIFO encoding helpers
//
// The PIO program PUSHes the ISR after N "in pins, 1" instructions.
// With in_shift_right=true, the first wire bit lands at ISR[31],
// then shifts right; after N IN ops the value reads:
//   ISR = b_{N-1}<<31 | b_{N-2}<<30 | ... | b_0<<(32-N)
// swd_phy_read_bits(N) shifts that right by (32-N) and returns
// b_0..b_{N-1} packed LSB-first in the low N bits.
//
// For tests, we work backwards from the value we want
// swd_phy_read_bits(N) to return:
//   raw_isr = value << (32 - N)
// -----------------------------------------------------------------------------

static uint32_t isr_for(uint32_t value, uint32_t bit_count) {
    return value << (32u - bit_count);
}

static void push_ack(swd_dp_ack_t ack) {
    hal_fake_pio_push_rx(PIO1, SM0, isr_for((uint32_t)ack, 3u));
}

static void push_data32(uint32_t v) {
    // 32 bits: shift by (32-32)=0, raw_isr = v.
    hal_fake_pio_push_rx(PIO1, SM0, v);
}

static void push_parity(uint8_t p) {
    hal_fake_pio_push_rx(PIO1, SM0, isr_for(p & 1u, 1u));
}

// FIFO TX command word decoders (mirror of test_swd_phy.c).
static uint32_t cmd_count(uint32_t w) {
    return (w & 0xFFu) + 1u;
}

static uint32_t write_data_after_cmd(uint32_t cmd_index) {
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    TEST_ASSERT_LESS_THAN(sm->tx_count, cmd_index + 1u);
    return sm->tx_fifo[cmd_index + 1u];
}

static void assert_write_at(uint32_t cmd_index, uint32_t bit_count, uint32_t expected_data) {
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    TEST_ASSERT_LESS_THAN(sm->tx_count, cmd_index);
    TEST_ASSERT_EQUAL_UINT32(bit_count, cmd_count(sm->tx_fifo[cmd_index]));
    TEST_ASSERT_EQUAL_HEX32(OD_INV32(expected_data), write_data_after_cmd(cmd_index));
}

// -----------------------------------------------------------------------------
// parity helper
// -----------------------------------------------------------------------------

static void test_parity_of_zero_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0u, swd_dp_compute_parity(0u));
}

static void test_parity_of_one_bit_is_one(void) {
    TEST_ASSERT_EQUAL_UINT8(1u, swd_dp_compute_parity(0x00000001u));
    TEST_ASSERT_EQUAL_UINT8(1u, swd_dp_compute_parity(0x80000000u));
}

static void test_parity_of_three_bits_is_one(void) {
    TEST_ASSERT_EQUAL_UINT8(1u, swd_dp_compute_parity(0b1011u));
}

static void test_parity_of_alternating_bits_is_zero(void) {
    TEST_ASSERT_EQUAL_UINT8(0u, swd_dp_compute_parity(0xFFFFFFFFu));
}

// -----------------------------------------------------------------------------
// DPIDR coherence helper
// -----------------------------------------------------------------------------

static void test_dpidr_validator_accepts_known_coherent_values(void) {
    TEST_ASSERT_TRUE(swd_dp_dpidr_is_valid(0x0BC12477u)); // RP2040
    TEST_ASSERT_TRUE(swd_dp_dpidr_is_valid(0x2BA01477u)); // common ARM Cortex-M DP
}

static void test_dpidr_validator_rejects_bus_noise_sentinels(void) {
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0x00000000u));
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0xFFFFFFFFu));
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0x0BC12476u)); // architected ID bit clear
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0x00001001u)); // empty designer / part
}

// -----------------------------------------------------------------------------
// Request build (verified via the actual TX FIFO emitted by swd_dp_read)
// -----------------------------------------------------------------------------

static void test_dp_read_dpidr_emits_request_byte_0xA5(void) {
    push_ack(SWD_ACK_OK);
    push_data32(0x0BC12477u); // RP2040 Cortex-M0+ DPIDR
    push_parity(swd_dp_compute_parity(0x0BC12477u));
    uint32_t dpidr = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_read(SWD_DP_ADDR_DPIDR, &dpidr));
    TEST_ASSERT_EQUAL_HEX32(0x0BC12477u, dpidr);
    // Find the 8-bit request word in TX FIFO. It is the data entry
    // immediately after the write-cmd command (count=8).
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found_request          = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(OD_INV8(0xA5u), (uint8_t)sm->tx_fifo[i + 1]);
            found_request = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_request);
}

static void test_dp_write_ctrlstat_emits_request_byte_0xA9(void) {
    // CTRLSTAT = addr 0x04. APnDP=0, RnW=0, A2=1, A3=0.
    // fields = (0<<1)|(0<<2)|(1<<3)|(0<<4) = 0x08; parity = 1.
    // request = 0x81 | 0x08 | (1<<5) = 0xA9.
    push_ack(SWD_ACK_OK);
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_write(SWD_DP_ADDR_CTRLSTAT, 0xDEADBEEFu));
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found                  = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(OD_INV8(0xA9u), (uint8_t)sm->tx_fifo[i + 1]);
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// -----------------------------------------------------------------------------
// ACK handling
// -----------------------------------------------------------------------------

static void test_dp_read_returns_ok_with_data_on_ack_ok(void) {
    push_ack(SWD_ACK_OK);
    push_data32(0x12345678u);
    push_parity(swd_dp_compute_parity(0x12345678u));
    uint32_t v = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_read(SWD_DP_ADDR_DPIDR, &v));
    TEST_ASSERT_EQUAL_HEX32(0x12345678u, v);
}

static void test_dp_read_returns_wait_on_ack_wait(void) {
    push_ack(SWD_ACK_WAIT);
    uint32_t v = 0xDEADu;
    TEST_ASSERT_EQUAL(SWD_ACK_WAIT, swd_dp_read(SWD_DP_ADDR_DPIDR, &v));
    TEST_ASSERT_EQUAL_HEX32(0xDEADu, v); // unchanged
}

static void test_dp_read_returns_fault_on_ack_fault(void) {
    push_ack(SWD_ACK_FAULT);
    uint32_t v = 0xDEADu;
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_dp_read(SWD_DP_ADDR_DPIDR, &v));
    TEST_ASSERT_EQUAL_HEX32(0xDEADu, v);
}

static void test_dp_read_returns_parity_err_on_bad_parity(void) {
    push_ack(SWD_ACK_OK);
    push_data32(0x12345678u);
    // Inverted parity bit — should trigger the parity error path
    // inside do_transfer.
    push_parity(swd_dp_compute_parity(0x12345678u) ^ 1u);
    uint32_t v = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_PARITY_ERR, swd_dp_read(SWD_DP_ADDR_DPIDR, &v));
}

static void test_dp_read_returns_no_target_when_swdio_stuck_high(void) {
    // ACK = 0b111 happens when SWDIO is held high (no pull-down,
    // floating, or no target). Codify as NO_TARGET.
    hal_fake_pio_push_rx(PIO1, SM0, isr_for(0b111u, 3u));
    uint32_t v = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_NO_TARGET, swd_dp_read(SWD_DP_ADDR_DPIDR, &v));
}

// -----------------------------------------------------------------------------
// Write data path
// -----------------------------------------------------------------------------

static void test_dp_write_emits_data_and_parity_after_request(void) {
    push_ack(SWD_ACK_OK);
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_write(SWD_DP_ADDR_CTRLSTAT, 0xCAFEBABEu));
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    // Walk the TX entries: find request (count=8), skip turnaround
    // command, expect another count=32 followed by data, then count=1
    // followed by parity.
    bool found_data         = false;
    bool found_parity       = false;
    uint8_t expected_parity = swd_dp_compute_parity(0xCAFEBABEu);
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 32u) {
            TEST_ASSERT_EQUAL_HEX32(OD_INV32(0xCAFEBABEu), sm->tx_fifo[i + 1]);
            found_data = true;
        }
        if (cmd_count(sm->tx_fifo[i]) == 1u &&
            (sm->tx_fifo[i] >> 8) & 1u) { // dir bit on → write mode SKIP or write_bits
            // After the 32-bit write, the next 1-bit command with
            // dir on is the parity bit. swd_phy_write_bits XOR-
            // inverts data before push (OD emulation), so the FIFO
            // entry equals ~expected_parity.
            uint32_t pdata = sm->tx_fifo[i + 1];
            if (pdata == OD_INV32((uint32_t)expected_parity)) {
                found_parity = true;
                break;
            }
        }
    }
    TEST_ASSERT_TRUE(found_data);
    TEST_ASSERT_TRUE(found_parity);
}

// -----------------------------------------------------------------------------
// Convenience wrappers
// -----------------------------------------------------------------------------

static void test_abort_targets_dp_address_zero(void) {
    push_ack(SWD_ACK_OK);
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_abort(SWD_ABORT_DAPABORT));
    // Request for write to addr 0, APnDP=0:
    //   fields = 0; parity = 0; req = 0x81.
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found                  = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(OD_INV8(0x81u), (uint8_t)sm->tx_fifo[i + 1]);
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

static void test_ap_read_sets_apndp_bit_in_request(void) {
    // bank_addr 0x00, AP read, RnW=1.
    //   fields = (1<<1)|(1<<2)|0|0 = 0x06; parity = 0.
    //   request = 0x81 | 0x06 = 0x87.
    push_ack(SWD_ACK_OK);
    push_data32(0u);
    push_parity(0u);
    uint32_t v = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_ap_read(0x00u, &v));
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found                  = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(OD_INV8(0x87u), (uint8_t)sm->tx_fifo[i + 1]);
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// -----------------------------------------------------------------------------
// wake-up / JTAG-to-SWD / IDCODE request / bus detect / connect
// -----------------------------------------------------------------------------

static void test_wakeup_emits_selection_alert_and_activation(void) {
    swd_dp_wakeup();

    // Each swd_phy_write_bits call emits command,data pairs.
    assert_write_at(0u, 8u, 0xffu);
    assert_write_at(2u, 32u, 0x6209f392u);
    assert_write_at(4u, 32u, 0x86852d95u);
    assert_write_at(6u, 32u, 0xe3ddafe9u);
    assert_write_at(8u, 32u, 0x19bc0ea2u);
    assert_write_at(10u, 4u, 0x0u);
    assert_write_at(12u, 8u, 0x1au);
}

static void test_switch_jtag_to_swd_emits_line_resets_and_command(void) {
    swd_dp_switch_jtag_to_swd();

    // 10 × 8-bit line-reset writes (80 bits ≥ the 50 required by ARM),
    // followed by the 16-bit JTAG_TO_SWD select command, followed by
    // another 10 × 8-bit line-reset block. See swd_dp.c:203.
    for (uint32_t i = 0; i < 10u; i++) {
        assert_write_at(i * 2u, 8u, 0xffu);
    }
    assert_write_at(20u, 16u, 0xe79eu);
    for (uint32_t i = 0; i < 10u; i++) {
        assert_write_at(22u + i * 2u, 8u, 0xffu);
    }
}

static void test_request_idcode_reads_dpidr_and_emits_idle(void) {
    push_ack(SWD_ACK_OK);
    push_data32(0x0BC12477u);
    push_parity(swd_dp_compute_parity(0x0BC12477u));

    uint32_t idcode = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_request_idcode(&idcode));
    TEST_ASSERT_EQUAL_HEX32(0x0BC12477u, idcode);

    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    assert_write_at(0u, 8u, 0xa5u);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2u, sm->tx_count);
    assert_write_at(sm->tx_count - 2u, 8u, 0x00u);
}

static void test_bus_detect_returns_ok_with_dpidr(void) {
    // swd_dp_bus_detect is intentionally composed from:
    //   wake-up -> JTAG-to-SWD -> IDCODE request.
    push_ack(SWD_ACK_OK);
    push_data32(0x0BC12477u);
    push_parity(swd_dp_compute_parity(0x0BC12477u));
    uint32_t dpidr = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_bus_detect(&dpidr));
    TEST_ASSERT_EQUAL_HEX32(0x0BC12477u, dpidr);
}

static void test_connect_returns_ok_with_dpidr_after_targetsel(void) {
    // swd_dp_connect performs the targeted SWDv2 multi-drop sequence:
    // dormant-to-SWD, TARGETSEL write, then DPIDR read. TARGETSEL ACK
    // clocks are discarded by design, so the first queued ACK is dummy.
    push_ack(SWD_ACK_NO_TARGET);
    push_ack(SWD_ACK_OK);
    push_data32(0x0BC12477u);
    push_parity(swd_dp_compute_parity(0x0BC12477u));
    uint32_t dpidr = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_connect(SWD_DP_TARGETSEL_RP2040_CORE0, &dpidr));
    TEST_ASSERT_EQUAL_HEX32(0x0BC12477u, dpidr);
}

static void test_connect_propagates_no_target(void) {
    hal_fake_pio_push_rx(PIO1, SM0, isr_for(0b111u, 3u));
    uint32_t dpidr = 0xDEADu;
    TEST_ASSERT_EQUAL(SWD_ACK_NO_TARGET, swd_dp_connect(SWD_DP_TARGETSEL_RP2040_CORE0, &dpidr));
    TEST_ASSERT_EQUAL_HEX32(0xDEADu, dpidr);
}

// -----------------------------------------------------------------------------
// swd_dp_power_up — clear sticky errors + request system & debug
// power-up + poll CTRL/STAT until both ACK bits are set.
//
// Wire sequence (per call):
//   1× write to ABORT  (1 ack on the wire)
//   1× write to CTRLSTAT (1 ack)
//   N× read  from CTRLSTAT (each = 1 ack + 32-bit data + 1-bit parity)
// -----------------------------------------------------------------------------

// Push the wire fixtures for a single CTRL/STAT read returning `value`.
static void push_ctrlstat_read(uint32_t value) {
    push_ack(SWD_ACK_OK);
    push_data32(value);
    push_parity(swd_dp_compute_parity(value));
}

static void test_power_up_acks_on_first_poll_returns_ok(void) {
    push_ack(SWD_ACK_OK);                       // abort
    push_ack(SWD_ACK_OK);                       // write CTRLSTAT
    push_ctrlstat_read(SWD_CTRLSTAT_PWRUP_ACK); // first poll already up
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_power_up(0));
}

static void test_power_up_waits_until_both_acks_are_set(void) {
    // First few polls return only CDBGPWRUPACK (missing the system
    // one) — power_up must keep polling until CSYSPWRUPACK shows up
    // too, then return OK.
    push_ack(SWD_ACK_OK);
    push_ack(SWD_ACK_OK);
    push_ctrlstat_read(0u);                        // poll 1: nothing
    push_ctrlstat_read(SWD_CTRLSTAT_CDBGPWRUPACK); // poll 2: half
    push_ctrlstat_read(SWD_CTRLSTAT_PWRUP_ACK);    // poll 3: full
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_power_up(0));
}

static void test_power_up_times_out_returns_wait(void) {
    // 5 polls all returning 0 → power_up must give up and return WAIT
    // (loop budget is per-call: pass max_retries=5).
    push_ack(SWD_ACK_OK);
    push_ack(SWD_ACK_OK);
    for (uint32_t i = 0u; i < 5u; ++i)
        push_ctrlstat_read(0u);
    TEST_ASSERT_EQUAL(SWD_ACK_WAIT, swd_dp_power_up(5u));
}

static void test_power_up_propagates_abort_failure(void) {
    // ABORT write FAULTs → power_up returns the FAULT verbatim and
    // never issues the CTRLSTAT request (nothing else is queued).
    push_ack(SWD_ACK_FAULT);
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_dp_power_up(0));
}

static void test_power_up_propagates_ctrlstat_write_failure(void) {
    push_ack(SWD_ACK_OK);   // abort OK
    push_ack(SWD_ACK_WAIT); // CTRLSTAT write WAITs
    TEST_ASSERT_EQUAL(SWD_ACK_WAIT, swd_dp_power_up(0));
}

static void test_power_up_propagates_ctrlstat_read_failure(void) {
    push_ack(SWD_ACK_OK);    // abort OK
    push_ack(SWD_ACK_OK);    // CTRLSTAT write OK
    push_ack(SWD_ACK_FAULT); // first poll FAULTs
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_dp_power_up(0));
}

static void test_power_up_writes_pwrup_req_to_ctrlstat(void) {
    push_ack(SWD_ACK_OK);                       // abort
    push_ack(SWD_ACK_OK);                       // write CTRLSTAT
    push_ctrlstat_read(SWD_CTRLSTAT_PWRUP_ACK); // poll OK
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_power_up(0));

    // Scan the TX FIFO for the 32-bit data word that follows the
    // CTRLSTAT write request (request byte = 0xA9, write to addr 4).
    // The expected payload is SWD_CTRLSTAT_PWRUP_REQ = 0x50000000,
    // XOR-inverted by swd_phy_write_bits for OD emulation.
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found_request          = false;
    bool found_payload          = false;
    for (uint32_t i = 0u; i + 1u < sm->tx_count; ++i) {
        if (cmd_count(sm->tx_fifo[i]) == 8u && (uint8_t)sm->tx_fifo[i + 1u] == OD_INV8(0xA9u)) {
            found_request = true;
            // The 32-bit data word lives at the next count==32 entry
            // after this request.
            for (uint32_t j = i + 2u; j + 1u < sm->tx_count; ++j) {
                if (cmd_count(sm->tx_fifo[j]) == 32u) {
                    TEST_ASSERT_EQUAL_HEX32(OD_INV32(SWD_CTRLSTAT_PWRUP_REQ), sm->tx_fifo[j + 1u]);
                    found_payload = true;
                    break;
                }
            }
            break;
        }
    }
    TEST_ASSERT_TRUE(found_request);
    TEST_ASSERT_TRUE(found_payload);
}

static void test_power_up_writes_sticky_clear_to_abort(void) {
    push_ack(SWD_ACK_OK);                       // abort
    push_ack(SWD_ACK_OK);                       // write CTRLSTAT
    push_ctrlstat_read(SWD_CTRLSTAT_PWRUP_ACK); // poll OK
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_power_up(0));

    // ABORT write request byte = 0x81 (addr 0, write). Look for the
    // first 8-bit TX entry that matches that byte and check the 32-
    // bit payload that follows is SWD_ABORT_ALL_STKY_CLR (0x1E).
    hal_fake_pio_sm_state_t* sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found_payload          = false;
    for (uint32_t i = 0u; i + 1u < sm->tx_count; ++i) {
        if (cmd_count(sm->tx_fifo[i]) == 8u && (uint8_t)sm->tx_fifo[i + 1u] == OD_INV8(0x81u)) {
            for (uint32_t j = i + 2u; j + 1u < sm->tx_count; ++j) {
                if (cmd_count(sm->tx_fifo[j]) == 32u) {
                    TEST_ASSERT_EQUAL_HEX32(OD_INV32(SWD_ABORT_ALL_STKY_CLR), sm->tx_fifo[j + 1u]);
                    found_payload = true;
                    break;
                }
            }
            break;
        }
    }
    TEST_ASSERT_TRUE(found_payload);
}

static void test_power_up_default_retry_budget_when_max_retries_zero(void) {
    // Passing 0 must fall back to the internal default (~1000).
    // Queue 1000 zero polls; if the default is honoured the function
    // returns WAIT and consumes them all. We only assert WAIT here —
    // confirming the exact default value would over-fit to an
    // implementation detail.
    push_ack(SWD_ACK_OK);
    push_ack(SWD_ACK_OK);
    for (uint32_t i = 0u; i < 1000u; ++i)
        push_ctrlstat_read(0u);
    TEST_ASSERT_EQUAL(SWD_ACK_WAIT, swd_dp_power_up(0));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parity_of_zero_is_zero);
    RUN_TEST(test_parity_of_one_bit_is_one);
    RUN_TEST(test_parity_of_three_bits_is_one);
    RUN_TEST(test_parity_of_alternating_bits_is_zero);
    RUN_TEST(test_dpidr_validator_accepts_known_coherent_values);
    RUN_TEST(test_dpidr_validator_rejects_bus_noise_sentinels);
    RUN_TEST(test_dp_read_dpidr_emits_request_byte_0xA5);
    RUN_TEST(test_dp_write_ctrlstat_emits_request_byte_0xA9);
    RUN_TEST(test_dp_read_returns_ok_with_data_on_ack_ok);
    RUN_TEST(test_dp_read_returns_wait_on_ack_wait);
    RUN_TEST(test_dp_read_returns_fault_on_ack_fault);
    RUN_TEST(test_dp_read_returns_parity_err_on_bad_parity);
    RUN_TEST(test_dp_read_returns_no_target_when_swdio_stuck_high);
    RUN_TEST(test_dp_write_emits_data_and_parity_after_request);
    RUN_TEST(test_abort_targets_dp_address_zero);
    RUN_TEST(test_ap_read_sets_apndp_bit_in_request);
    RUN_TEST(test_wakeup_emits_selection_alert_and_activation);
    RUN_TEST(test_switch_jtag_to_swd_emits_line_resets_and_command);
    RUN_TEST(test_request_idcode_reads_dpidr_and_emits_idle);
    RUN_TEST(test_bus_detect_returns_ok_with_dpidr);
    RUN_TEST(test_connect_returns_ok_with_dpidr_after_targetsel);
    RUN_TEST(test_connect_propagates_no_target);
    RUN_TEST(test_power_up_acks_on_first_poll_returns_ok);
    RUN_TEST(test_power_up_waits_until_both_acks_are_set);
    RUN_TEST(test_power_up_times_out_returns_wait);
    RUN_TEST(test_power_up_propagates_abort_failure);
    RUN_TEST(test_power_up_propagates_ctrlstat_write_failure);
    RUN_TEST(test_power_up_propagates_ctrlstat_read_failure);
    RUN_TEST(test_power_up_writes_pwrup_req_to_ctrlstat);
    RUN_TEST(test_power_up_writes_sticky_clear_to_abort);
    RUN_TEST(test_power_up_default_retry_budget_when_max_retries_zero);
    return UNITY_END();
}

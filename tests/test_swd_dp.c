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
static uint32_t cmd_count(uint32_t w) { return (w & 0xFFu) + 1u; }

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
// Request build (verified via the actual TX FIFO emitted by swd_dp_read)
// -----------------------------------------------------------------------------

static void test_dp_read_dpidr_emits_request_byte_0xA5(void) {
    push_ack(SWD_ACK_OK);
    push_data32(0x0BC12477u);   // RP2040 Cortex-M0+ DPIDR
    push_parity(swd_dp_compute_parity(0x0BC12477u));
    uint32_t dpidr = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_read(SWD_DP_ADDR_DPIDR, &dpidr));
    TEST_ASSERT_EQUAL_HEX32(0x0BC12477u, dpidr);
    // Find the 8-bit request word in TX FIFO. It is the data entry
    // immediately after the write-cmd command (count=8).
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found_request = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(0xA5u, (uint8_t)sm->tx_fifo[i + 1]);
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
    TEST_ASSERT_EQUAL(SWD_ACK_OK,
        swd_dp_write(SWD_DP_ADDR_CTRLSTAT, 0xDEADBEEFu));
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(0xA9u, (uint8_t)sm->tx_fifo[i + 1]);
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
    TEST_ASSERT_EQUAL_HEX32(0xDEADu, v);  // unchanged
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
    TEST_ASSERT_EQUAL(SWD_ACK_PARITY_ERR,
                      swd_dp_read(SWD_DP_ADDR_DPIDR, &v));
}

static void test_dp_read_returns_no_target_when_swdio_stuck_high(void) {
    // ACK = 0b111 happens when SWDIO is held high (no pull-down,
    // floating, or no target). Codify as NO_TARGET.
    hal_fake_pio_push_rx(PIO1, SM0, isr_for(0b111u, 3u));
    uint32_t v = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_NO_TARGET,
                      swd_dp_read(SWD_DP_ADDR_DPIDR, &v));
}

// -----------------------------------------------------------------------------
// Write data path
// -----------------------------------------------------------------------------

static void test_dp_write_emits_data_and_parity_after_request(void) {
    push_ack(SWD_ACK_OK);
    TEST_ASSERT_EQUAL(SWD_ACK_OK,
        swd_dp_write(SWD_DP_ADDR_CTRLSTAT, 0xCAFEBABEu));
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    // Walk the TX entries: find request (count=8), skip turnaround
    // command, expect another count=32 followed by data, then count=1
    // followed by parity.
    bool found_data = false;
    bool found_parity = false;
    uint8_t expected_parity = swd_dp_compute_parity(0xCAFEBABEu);
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 32u) {
            TEST_ASSERT_EQUAL_HEX32(0xCAFEBABEu, sm->tx_fifo[i + 1]);
            found_data = true;
        }
        if (cmd_count(sm->tx_fifo[i]) == 1u
         && (sm->tx_fifo[i] >> 8) & 1u) {  // dir bit on → write mode SKIP or write_bits
            // Distinguish: after the 32-bit write, the next 1-bit
            // command with dir on is the parity bit. Just check the
            // following data word matches the expected parity.
            uint32_t pdata = sm->tx_fifo[i + 1];
            if (pdata == (uint32_t)expected_parity
             || pdata == 0u) {  // 0 only when expected_parity == 0
                found_parity = (pdata == (uint32_t)expected_parity);
                if (found_parity) break;
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
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(0x81u, (uint8_t)sm->tx_fifo[i + 1]);
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
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found = false;
    for (uint32_t i = 0; i + 1 < sm->tx_count; i++) {
        if (cmd_count(sm->tx_fifo[i]) == 8u) {
            TEST_ASSERT_EQUAL_HEX8(0x87u, (uint8_t)sm->tx_fifo[i + 1]);
            found = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

// -----------------------------------------------------------------------------
// connect — line reset + JTAG-to-SWD switch + DPIDR read
// -----------------------------------------------------------------------------

static void test_connect_returns_ok_with_dpidr(void) {
    // Pre-populate the read sequence the final swd_dp_read_dpidr
    // performs; the line-reset writes go to TX (some are dropped by
    // the fake's 16-entry FIFO, harmless for this test).
    push_ack(SWD_ACK_OK);
    push_data32(0x0BC12477u);
    push_parity(swd_dp_compute_parity(0x0BC12477u));
    uint32_t dpidr = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_connect(&dpidr));
    TEST_ASSERT_EQUAL_HEX32(0x0BC12477u, dpidr);
}

static void test_connect_propagates_no_target(void) {
    hal_fake_pio_push_rx(PIO1, SM0, isr_for(0b111u, 3u));
    uint32_t dpidr = 0xDEADu;
    TEST_ASSERT_EQUAL(SWD_ACK_NO_TARGET, swd_dp_connect(&dpidr));
    TEST_ASSERT_EQUAL_HEX32(0xDEADu, dpidr);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parity_of_zero_is_zero);
    RUN_TEST(test_parity_of_one_bit_is_one);
    RUN_TEST(test_parity_of_three_bits_is_one);
    RUN_TEST(test_parity_of_alternating_bits_is_zero);
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
    RUN_TEST(test_connect_returns_ok_with_dpidr);
    RUN_TEST(test_connect_propagates_no_target);
    return UNITY_END();
}

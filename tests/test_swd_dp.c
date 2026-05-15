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
#define OD_INV8(x)  ((uint8_t)~(uint8_t)(x))
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
static uint32_t cmd_count(uint32_t w) { return (w & 0xFFu) + 1u; }

static uint32_t write_data_after_cmd(uint32_t cmd_index) {
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    TEST_ASSERT_LESS_THAN(sm->tx_count, cmd_index + 1u);
    return sm->tx_fifo[cmd_index + 1u];
}

static void assert_write_at(uint32_t cmd_index, uint32_t bit_count,
                            uint32_t expected_data) {
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
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
    TEST_ASSERT_TRUE(swd_dp_dpidr_is_valid(0x0BC12477u));  // RP2040
    TEST_ASSERT_TRUE(swd_dp_dpidr_is_valid(0x2BA01477u));  // common ARM Cortex-M DP
}

static void test_dpidr_validator_rejects_bus_noise_sentinels(void) {
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0x00000000u));
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0xFFFFFFFFu));
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0x0BC12476u));  // architected ID bit clear
    TEST_ASSERT_FALSE(swd_dp_dpidr_is_valid(0x00001001u));  // empty designer / part
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
    TEST_ASSERT_EQUAL(SWD_ACK_OK,
        swd_dp_write(SWD_DP_ADDR_CTRLSTAT, 0xDEADBEEFu));
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found = false;
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
            TEST_ASSERT_EQUAL_HEX32(OD_INV32(0xCAFEBABEu), sm->tx_fifo[i + 1]);
            found_data = true;
        }
        if (cmd_count(sm->tx_fifo[i]) == 1u
         && (sm->tx_fifo[i] >> 8) & 1u) {  // dir bit on → write mode SKIP or write_bits
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
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found = false;
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
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found = false;
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
    assert_write_at(0u,  8u,  0xffu);
    assert_write_at(2u,  32u, 0x6209f392u);
    assert_write_at(4u,  32u, 0x86852d95u);
    assert_write_at(6u,  32u, 0xe3ddafe9u);
    assert_write_at(8u,  32u, 0x19bc0ea2u);
    assert_write_at(10u, 4u,  0x0u);
    assert_write_at(12u, 8u,  0x1au);
}

static void test_switch_jtag_to_swd_emits_line_resets_and_command(void) {
    swd_dp_switch_jtag_to_swd();

    for (uint32_t i = 0; i < 7u; i++) {
        assert_write_at(i * 2u, 8u, 0xffu);
    }
    assert_write_at(14u, 16u, 0xe79eu);
    for (uint32_t i = 0; i < 7u; i++) {
        assert_write_at(16u + i * 2u, 8u, 0xffu);
    }
}

static void test_request_idcode_reads_dpidr_and_emits_idle(void) {
    push_ack(SWD_ACK_OK);
    push_data32(0x0BC12477u);
    push_parity(swd_dp_compute_parity(0x0BC12477u));

    uint32_t idcode = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_dp_request_idcode(&idcode));
    TEST_ASSERT_EQUAL_HEX32(0x0BC12477u, idcode);

    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
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
    return UNITY_END();
}

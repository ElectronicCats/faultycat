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

// --- safety contracts (code-review additions) ---

void test_pio_try_put_false_when_not_inited(void) {
    // No init: try_put must return false, not crash or silently succeed.
    TEST_ASSERT_FALSE(target_serial_pio_try_put(0x55u));
}

void test_pio_try_get_false_when_not_inited(void) {
    // No init: try_get must return false even with a valid out pointer.
    uint8_t b;
    TEST_ASSERT_FALSE(target_serial_pio_try_get(&b));
}

void test_pio_try_get_null_guard(void) {
    // NULL out pointer must return false even when the SM is running.
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    TEST_ASSERT_FALSE(target_serial_pio_try_get(NULL));
    target_serial_pio_deinit();
}

void test_pio_deinit_restores_rx_pull(void) {
    // After deinit, GP5 (RX) pull-up must be cleared so scanner_io /
    // swd_phy can re-claim the pin cleanly. Mirrors the
    // test_deinit_unclaims_and_restores_pins pattern from test_swd_phy.c.
    TEST_ASSERT_TRUE(target_serial_pio_init(4u, 5u, 136u));
    target_serial_pio_deinit();
    TEST_ASSERT_FALSE(hal_fake_gpio_states[5].pull_up);
    // Pin must also be left as input (not driven).
    TEST_ASSERT_FALSE((hal_fake_gpio_states[5].dir == HAL_GPIO_DIR_OUT));
}

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_pio_init_claims_both_sms_and_loads_programs);
    RUN_TEST(test_pio_init_routes_pins);
    RUN_TEST(test_pio_deinit_unclaims_both_sms);
    RUN_TEST(test_pio_try_put_pushes_to_tx_fifo);
    RUN_TEST(test_pio_try_get_reads_high_byte_of_word);
    RUN_TEST(test_pio_set_divider_updates_both_sms);
    RUN_TEST(test_pio_try_put_false_when_not_inited);
    RUN_TEST(test_pio_try_get_false_when_not_inited);
    RUN_TEST(test_pio_try_get_null_guard);
    RUN_TEST(test_pio_deinit_restores_rx_pull);
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
    return UNITY_END();
}

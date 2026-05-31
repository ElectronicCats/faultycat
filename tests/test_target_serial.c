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
    return UNITY_END();
}

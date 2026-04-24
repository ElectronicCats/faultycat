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

static void test_deinit_detaches_driver_too(void) {
    // Guards against the desync where deinit unclaims the SM but the
    // driver stays marked attached — leaving CPU fire permanently
    // refused.
    emfi_pio_init();
    emfi_pio_params_t p = { .trigger = EMFI_TRIG_IMMEDIATE,
                           .delay_us = 10, .width_us = 5 };
    TEST_ASSERT_TRUE(emfi_pio_load(&p));
    TEST_ASSERT_TRUE(emfi_pulse_is_attached_to_pio());
    emfi_pio_deinit();
    TEST_ASSERT_FALSE(emfi_pulse_is_attached_to_pio());
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
    RUN_TEST(test_deinit_detaches_driver_too);
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

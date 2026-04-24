// Unit tests for services/glitch_engine/crowbar/crowbar_pio —
// exercised against hal fakes. Crowbar runs on pio0/SM1 alongside
// EMFI on pio0/SM0.

#include "unity.h"

#include "board_v2.h"
#include "crowbar_pio.h"
#include "hal/pio.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"

void setUp(void) {
    hal_fake_pio_reset();
    hal_fake_gpio_reset();
}

void tearDown(void) {
    crowbar_pio_deinit();
}

static void test_init_claims_pio0_sm1(void) {
    TEST_ASSERT_TRUE(crowbar_pio_init());
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[1].claimed);
    // SM 0 (EMFI) must be left untouched.
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[0].claimed);
}

static void test_init_fails_if_sm1_already_claimed(void) {
    hal_pio_claim_sm(hal_pio_instance(0), 1u);
    TEST_ASSERT_FALSE(crowbar_pio_init());
}

static void test_deinit_unclaims_and_drops_program(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 10u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    crowbar_pio_deinit();
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[1].claimed);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].program.loaded);
}

static void test_load_rejects_zero_width(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 0u, .width_ns = 0u };
    TEST_ASSERT_FALSE(crowbar_pio_load(&p));
}

static void test_load_rejects_width_below_min(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 0u, .width_ns = 7u };
    TEST_ASSERT_FALSE(crowbar_pio_load(&p));
}

static void test_load_rejects_width_above_max(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 0u,
                              .width_ns = CROWBAR_PIO_WIDTH_NS_MAX + 1u };
    TEST_ASSERT_FALSE(crowbar_pio_load(&p));
}

static void test_load_rejects_delay_above_max(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = CROWBAR_PIO_DELAY_US_MAX + 1u,
                              .width_ns = 200u };
    TEST_ASSERT_FALSE(crowbar_pio_load(&p));
}

static void test_load_rejects_output_none(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_NONE,
                              .delay_us = 0u, .width_ns = 200u };
    TEST_ASSERT_FALSE(crowbar_pio_load(&p));
}

static void test_load_immediate_has_no_trigger_block(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 1u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    // Same shape as EMFI: 2 (delay setup) + 0 (trigger) + 1 (delay
    // loop) + 2 (width setup) + 1 (SET high) + 1 (hold loop) + 1
    // (SET low) + 1 (IRQ) = 9.
    TEST_ASSERT_EQUAL_UINT32(9u, hal_fake_pio_insts[0].program.length);
}

static void test_load_rising_edge_inserts_two_waits(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_EXT_RISING,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 1u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    TEST_ASSERT_EQUAL_UINT32(11u, hal_fake_pio_insts[0].program.length);
    TEST_ASSERT_EQUAL_HEX16(0x2020, hal_fake_pio_insts[0].program.instructions[2]);
    TEST_ASSERT_EQUAL_HEX16(0x20A0, hal_fake_pio_insts[0].program.instructions[3]);
}

static void test_load_pulse_positive_inserts_three_waits(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_EXT_PULSE_POS,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 1u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    TEST_ASSERT_EQUAL_UINT32(12u, hal_fake_pio_insts[0].program.length);
}

static void test_load_lp_binds_gp16_to_pio(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_LP,
                              .delay_us = 1u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].gpio_init_bitmap
                     & (1u << BOARD_GP_CROWBAR_LP));
    TEST_ASSERT_EQUAL_UINT32(BOARD_GP_CROWBAR_LP,
                             hal_fake_pio_insts[0].sm[1].last_cfg.set_pin_base);
}

static void test_load_hp_binds_gp17_to_pio(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 1u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].gpio_init_bitmap
                     & (1u << BOARD_GP_CROWBAR_HP));
    TEST_ASSERT_EQUAL_UINT32(BOARD_GP_CROWBAR_HP,
                             hal_fake_pio_insts[0].sm[1].last_cfg.set_pin_base);
}

static void test_load_binds_ext_trigger_as_in_pin(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_EXT_RISING,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 1u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    TEST_ASSERT_EQUAL_UINT32(BOARD_GP_EXT_TRIGGER,
                             hal_fake_pio_insts[0].sm[1].last_cfg.in_pin_base);
}

static void test_load_uses_irq1_opcode(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 0u, .width_ns = 200u };
    TEST_ASSERT_TRUE(crowbar_pio_load(&p));
    uint32_t len = hal_fake_pio_insts[0].program.length;
    // Last instruction is the IRQ — must be 0xC001 (IRQ index 1) so
    // crowbar does not collide with EMFI's IRQ 0.
    TEST_ASSERT_EQUAL_HEX16(0xC001,
                            hal_fake_pio_insts[0].program.instructions[len - 1]);
}

static void test_start_pushes_delay_then_width_ticks(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 10u, .width_ns = 400u };
    crowbar_pio_load(&p);
    TEST_ASSERT_TRUE(crowbar_pio_start());
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[1].tx_count);
    TEST_ASSERT_EQUAL_UINT32(10u * 125u,
                             hal_fake_pio_insts[0].sm[1].tx_fifo[0]);
    // 400 ns / 8 ns per tick = 50 ticks.
    TEST_ASSERT_EQUAL_UINT32(50u,
                             hal_fake_pio_insts[0].sm[1].tx_fifo[1]);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[1].enabled);
}

static void test_start_width_rounds_up_for_fractional_tick(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 0u, .width_ns = 9u };
    crowbar_pio_load(&p);
    crowbar_pio_start();
    // 9 ns rounds up to 2 ticks (16 ns) — never floors to 1.
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[1].tx_fifo[1]);
}

static void test_start_width_at_min_is_one_tick(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 0u, .width_ns = 8u };
    crowbar_pio_load(&p);
    crowbar_pio_start();
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[0].sm[1].tx_fifo[1]);
}

static void test_is_done_polls_irq1(void) {
    crowbar_pio_init();
    crowbar_pio_params_t p = { .trigger  = CROWBAR_TRIG_IMMEDIATE,
                              .output   = CROWBAR_OUT_HP,
                              .delay_us = 1u, .width_ns = 200u };
    crowbar_pio_load(&p);
    crowbar_pio_start();
    TEST_ASSERT_FALSE(crowbar_pio_is_done());
    // Raising IRQ 0 (EMFI's flag) must NOT make crowbar think it's done.
    hal_fake_pio_raise_irq(0, 0);
    TEST_ASSERT_FALSE(crowbar_pio_is_done());
    hal_fake_pio_raise_irq(0, 1);
    TEST_ASSERT_TRUE(crowbar_pio_is_done());
    crowbar_pio_clear_done();
    TEST_ASSERT_FALSE(crowbar_pio_is_done());
}

static void test_ns_per_tick_is_8(void) {
    TEST_ASSERT_EQUAL_UINT32(8u, crowbar_pio_ns_per_tick());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_claims_pio0_sm1);
    RUN_TEST(test_init_fails_if_sm1_already_claimed);
    RUN_TEST(test_deinit_unclaims_and_drops_program);
    RUN_TEST(test_load_rejects_zero_width);
    RUN_TEST(test_load_rejects_width_below_min);
    RUN_TEST(test_load_rejects_width_above_max);
    RUN_TEST(test_load_rejects_delay_above_max);
    RUN_TEST(test_load_rejects_output_none);
    RUN_TEST(test_load_immediate_has_no_trigger_block);
    RUN_TEST(test_load_rising_edge_inserts_two_waits);
    RUN_TEST(test_load_pulse_positive_inserts_three_waits);
    RUN_TEST(test_load_lp_binds_gp16_to_pio);
    RUN_TEST(test_load_hp_binds_gp17_to_pio);
    RUN_TEST(test_load_binds_ext_trigger_as_in_pin);
    RUN_TEST(test_load_uses_irq1_opcode);
    RUN_TEST(test_start_pushes_delay_then_width_ticks);
    RUN_TEST(test_start_width_rounds_up_for_fractional_tick);
    RUN_TEST(test_start_width_at_min_is_one_tick);
    RUN_TEST(test_is_done_polls_irq1);
    RUN_TEST(test_ns_per_tick_is_8);
    return UNITY_END();
}

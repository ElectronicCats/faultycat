// Unit tests for drivers/emfi_pulse — exercised against hal_fake.

#include "unity.h"

#include "emfi_pulse.h"
#include "board_v2.h"
#include "hal/pio.h"
#include "hal/time.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
}

void tearDown(void) {
}

// -----------------------------------------------------------------------------
// init — HV pulse output starts at the safe level (LOW)
// -----------------------------------------------------------------------------

static void test_init_configures_gp14_as_output_low(void) {
    emfi_pulse_init();
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].initialized);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_HV_PULSE].dir);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_init_disables_pulls(void) {
    emfi_pulse_init();
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].pull_down);
}

static void test_force_low_drives_pin_low(void) {
    emfi_pulse_init();
    hal_fake_gpio_states[BOARD_GP_HV_PULSE].level = true; // simulate unexpected state
    emfi_pulse_force_low();
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

// -----------------------------------------------------------------------------
// fire_manual — pulse width range + end state + cool-down
// -----------------------------------------------------------------------------

static void test_fire_manual_rejects_zero_width(void) {
    emfi_pulse_init();
    TEST_ASSERT_FALSE(emfi_pulse_fire_manual(0u));
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_fire_manual_rejects_width_above_safety_max(void) {
    emfi_pulse_init();
    TEST_ASSERT_FALSE(emfi_pulse_fire_manual(EMFI_PULSE_MAX_WIDTH_US + 1u));
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_fire_manual_accepts_min_width(void) {
    emfi_pulse_init();
    TEST_ASSERT_TRUE(emfi_pulse_fire_manual(EMFI_PULSE_MIN_WIDTH_US));
    // End state: pin back to LOW.
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_fire_manual_accepts_max_width(void) {
    emfi_pulse_init();
    TEST_ASSERT_TRUE(emfi_pulse_fire_manual(EMFI_PULSE_MAX_WIDTH_US));
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_fire_manual_calls_put_high_then_low(void) {
    emfi_pulse_init();
    uint32_t puts_before = hal_fake_gpio_states[BOARD_GP_HV_PULSE].put_calls;
    TEST_ASSERT_TRUE(emfi_pulse_fire_manual(5u));
    // init drove the pin low once, force_low is not called by fire,
    // fire itself writes high then low = 2 puts.
    TEST_ASSERT_EQUAL_UINT32(puts_before + 2u,
                             hal_fake_gpio_states[BOARD_GP_HV_PULSE].put_calls);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
}

static void test_fire_manual_advances_clock_by_width_plus_cooldown(void) {
    emfi_pulse_init();
    // fake time already reset; clock at 0.
    uint64_t before_us = (uint64_t)hal_now_us();
    TEST_ASSERT_TRUE(emfi_pulse_fire_manual(5u));
    uint32_t now_ms = hal_now_ms();
    // width 5us + cool-down 250ms = 250_005 us = 250 ms when
    // truncated.
    TEST_ASSERT_EQUAL_UINT32(EMFI_PULSE_COOLDOWN_MS, now_ms);
    (void)before_us;
}

static void test_fire_manual_does_not_leave_pin_high(void) {
    emfi_pulse_init();
    // Even with the maximum allowed width, the pin must be LOW
    // after fire returns.
    emfi_pulse_fire_manual(EMFI_PULSE_MAX_WIDTH_US);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PULSE].level);
    TEST_ASSERT_FALSE(hal_gpio_get(BOARD_GP_HV_PULSE));
}

// -----------------------------------------------------------------------------
// F4-3 — PIO attach/detach interaction with CPU fire path
// -----------------------------------------------------------------------------

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

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_gp14_as_output_low);
    RUN_TEST(test_init_disables_pulls);
    RUN_TEST(test_force_low_drives_pin_low);
    RUN_TEST(test_fire_manual_rejects_zero_width);
    RUN_TEST(test_fire_manual_rejects_width_above_safety_max);
    RUN_TEST(test_fire_manual_accepts_min_width);
    RUN_TEST(test_fire_manual_accepts_max_width);
    RUN_TEST(test_fire_manual_calls_put_high_then_low);
    RUN_TEST(test_fire_manual_advances_clock_by_width_plus_cooldown);
    RUN_TEST(test_fire_manual_does_not_leave_pin_high);
    RUN_TEST(test_attach_pio_succeeds_when_detached);
    RUN_TEST(test_attach_pio_refuses_when_already_attached);
    RUN_TEST(test_fire_manual_rejected_while_attached);
    RUN_TEST(test_detach_pio_returns_pin_to_gpio_low);
    RUN_TEST(test_fire_manual_works_again_after_detach);
    RUN_TEST(test_force_low_detaches_if_attached);
    return UNITY_END();
}

// Unit tests for drivers/hv_charger — exercised against hal_fake.
//
// The driver's core safety property is "DISARMED at boot + auto-
// disarm after the configured timeout". Most cases here verify that
// property exhaustively.

#include "unity.h"

#include "hv_charger.h"
#include "board_v2.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pwm.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_pwm_reset();
    hal_fake_time_reset();
}

void tearDown(void) {
}

// -----------------------------------------------------------------------------
// init — safety: DISARMED at boot with GP20 driven LOW
// -----------------------------------------------------------------------------

static void test_init_leaves_charger_disarmed(void) {
    hv_charger_init();
    TEST_ASSERT_FALSE(hv_charger_is_armed());
    TEST_ASSERT_FALSE(hal_fake_pwm_states[BOARD_GP_HV_PWM].enabled);
}

static void test_init_drives_pwm_pin_low_as_plain_gpio(void) {
    hv_charger_init();
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_HV_PWM].initialized);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_HV_PWM].dir);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PWM].level);
}

static void test_init_configures_charged_feedback_as_input(void) {
    hv_charger_init();
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_HV_CHARGED].initialized);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[BOARD_GP_HV_CHARGED].dir);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_CHARGED].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_CHARGED].pull_down);
}

static void test_init_defaults_match_legacy_values(void) {
    hv_charger_init();
    hv_charger_config_t cfg;
    hv_charger_get_config(&cfg);
    TEST_ASSERT_EQUAL_FLOAT(2500.0f, cfg.flyback_freq_hz);
    TEST_ASSERT_EQUAL_FLOAT(0.0122f, cfg.flyback_duty);
    TEST_ASSERT_EQUAL_UINT32(60000u, cfg.auto_disarm_ms);
}

// -----------------------------------------------------------------------------
// arm / disarm
// -----------------------------------------------------------------------------

static void test_arm_enables_pwm_with_configured_values(void) {
    hv_charger_init();
    hv_charger_arm();

    TEST_ASSERT_TRUE(hv_charger_is_armed());
    TEST_ASSERT_TRUE(hal_fake_pwm_states[BOARD_GP_HV_PWM].enabled);
    TEST_ASSERT_EQUAL_FLOAT(2500.0f, hal_fake_pwm_states[BOARD_GP_HV_PWM].last_freq);
    TEST_ASSERT_EQUAL_FLOAT(0.0122f, hal_fake_pwm_states[BOARD_GP_HV_PWM].last_duty);
}

static void test_disarm_disables_pwm_and_drives_pin_low(void) {
    hv_charger_init();
    hv_charger_arm();
    hv_charger_disarm();

    TEST_ASSERT_FALSE(hv_charger_is_armed());
    TEST_ASSERT_FALSE(hal_fake_pwm_states[BOARD_GP_HV_PWM].enabled);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_HV_PWM].level);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_HV_PWM].dir);
}

static void test_arm_is_idempotent(void) {
    hv_charger_init();
    hv_charger_arm();
    hv_charger_arm();
    TEST_ASSERT_TRUE(hv_charger_is_armed());
}

static void test_disarm_is_idempotent(void) {
    hv_charger_init();
    hv_charger_disarm();
    hv_charger_disarm();
    TEST_ASSERT_FALSE(hv_charger_is_armed());
}

// -----------------------------------------------------------------------------
// is_charged — polarity normalization (GP18 is ACTIVE LOW)
// -----------------------------------------------------------------------------

static void test_is_charged_reads_active_low_signal(void) {
    hv_charger_init();

    // Signal LOW on the pin → logically "charged" (true)
    hal_fake_gpio_states[BOARD_GP_HV_CHARGED].level = false;
    TEST_ASSERT_TRUE(hv_charger_is_charged());

    // Signal HIGH on the pin → not charged
    hal_fake_gpio_states[BOARD_GP_HV_CHARGED].level = true;
    TEST_ASSERT_FALSE(hv_charger_is_charged());
}

// -----------------------------------------------------------------------------
// auto-disarm — the single most safety-critical behaviour
// -----------------------------------------------------------------------------

static void test_tick_does_not_disarm_before_timeout(void) {
    hv_charger_init();
    hv_charger_arm();

    hal_fake_time_advance_us(59u * 1000u * 1000u);  // 59 s elapsed
    hv_charger_tick();
    TEST_ASSERT_TRUE(hv_charger_is_armed());
}

static void test_tick_disarms_exactly_at_timeout(void) {
    hv_charger_init();
    hv_charger_arm();

    hal_fake_time_advance_us(60u * 1000u * 1000u);  // exactly 60 s
    hv_charger_tick();
    TEST_ASSERT_FALSE(hv_charger_is_armed());
}

static void test_tick_disarms_past_timeout(void) {
    hv_charger_init();
    hv_charger_arm();

    hal_fake_time_advance_us(120u * 1000u * 1000u);  // 2 minutes — well past
    hv_charger_tick();
    TEST_ASSERT_FALSE(hv_charger_is_armed());
}

static void test_tick_is_noop_when_disarmed(void) {
    hv_charger_init();
    // init itself drives the PWM pin low, which counts as one
    // disable call — snapshot that baseline before the tick.
    uint32_t baseline_disable = hal_fake_pwm_states[BOARD_GP_HV_PWM].disable_calls;

    hal_fake_time_advance_us(120u * 1000u * 1000u);
    hv_charger_tick();

    TEST_ASSERT_FALSE(hv_charger_is_armed());
    // Tick on a disarmed charger MUST not add any further disables.
    TEST_ASSERT_EQUAL_UINT32(baseline_disable,
                             hal_fake_pwm_states[BOARD_GP_HV_PWM].disable_calls);
}

static void test_arm_resets_timeout_window(void) {
    hv_charger_init();
    hv_charger_arm();

    // Almost-timeout, then re-arm.
    hal_fake_time_advance_us(55u * 1000u * 1000u);
    hv_charger_arm();   // restart the clock

    // 55 s past the *original* arm, but 0 s past the re-arm.
    hv_charger_tick();
    TEST_ASSERT_TRUE(hv_charger_is_armed());

    // 30 s past the re-arm — still armed.
    hal_fake_time_advance_us(30u * 1000u * 1000u);
    hv_charger_tick();
    TEST_ASSERT_TRUE(hv_charger_is_armed());

    // 65 s past the re-arm — disarmed.
    hal_fake_time_advance_us(35u * 1000u * 1000u);
    hv_charger_tick();
    TEST_ASSERT_FALSE(hv_charger_is_armed());
}

static void test_timeout_of_zero_disables_auto_disarm(void) {
    hv_charger_init();
    hv_charger_config_t cfg = {
        .flyback_freq_hz = 2500.0f,
        .flyback_duty    = 0.01f,
        .auto_disarm_ms  = 0u,  // explicitly disabled
    };
    hv_charger_configure(&cfg);
    hv_charger_arm();

    hal_fake_time_advance_us(10u * 60u * 1000u * 1000u);  // 10 minutes
    hv_charger_tick();
    TEST_ASSERT_TRUE(hv_charger_is_armed());
}

// -----------------------------------------------------------------------------
// configure
// -----------------------------------------------------------------------------

static void test_configure_overrides_next_arm_values(void) {
    hv_charger_init();
    hv_charger_config_t cfg = {
        .flyback_freq_hz = 5000.0f,
        .flyback_duty    = 0.02f,
        .auto_disarm_ms  = 30000u,
    };
    hv_charger_configure(&cfg);
    hv_charger_arm();

    TEST_ASSERT_EQUAL_FLOAT(5000.0f, hal_fake_pwm_states[BOARD_GP_HV_PWM].last_freq);
    TEST_ASSERT_EQUAL_FLOAT(0.02f,   hal_fake_pwm_states[BOARD_GP_HV_PWM].last_duty);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_leaves_charger_disarmed);
    RUN_TEST(test_init_drives_pwm_pin_low_as_plain_gpio);
    RUN_TEST(test_init_configures_charged_feedback_as_input);
    RUN_TEST(test_init_defaults_match_legacy_values);
    RUN_TEST(test_arm_enables_pwm_with_configured_values);
    RUN_TEST(test_disarm_disables_pwm_and_drives_pin_low);
    RUN_TEST(test_arm_is_idempotent);
    RUN_TEST(test_disarm_is_idempotent);
    RUN_TEST(test_is_charged_reads_active_low_signal);
    RUN_TEST(test_tick_does_not_disarm_before_timeout);
    RUN_TEST(test_tick_disarms_exactly_at_timeout);
    RUN_TEST(test_tick_disarms_past_timeout);
    RUN_TEST(test_tick_is_noop_when_disarmed);
    RUN_TEST(test_arm_resets_timeout_window);
    RUN_TEST(test_timeout_of_zero_disables_auto_disarm);
    RUN_TEST(test_configure_overrides_next_arm_values);
    return UNITY_END();
}

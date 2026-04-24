// Unit tests for services/glitch_engine/crowbar/crowbar_campaign.

#include "unity.h"

#include "board_v2.h"
#include "crowbar_campaign.h"
#include "crowbar_mosfet.h"
#include "crowbar_pio.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
    hal_fake_pio_reset();
    crowbar_mosfet_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    crowbar_campaign_init();
}

void tearDown(void) {
    crowbar_campaign_disarm();
}

static crowbar_config_t default_cfg(void) {
    crowbar_config_t c = {
        .trigger  = CROWBAR_TRIG_IMMEDIATE,
        .output   = CROWBAR_OUT_HP,
        .delay_us = 10u,
        .width_ns = 200u,
    };
    return c;
}

static void advance_ms(uint32_t ms) { hal_fake_time_advance_ms(ms); }

// -----------------------------------------------------------------------------
// init / configure
// -----------------------------------------------------------------------------

static void test_initial_state_is_idle(void) {
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_IDLE, s.state);
    TEST_ASSERT_EQUAL(CROWBAR_ERR_NONE, s.err);
    TEST_ASSERT_EQUAL(CROWBAR_OUT_NONE, s.output);
}

static void test_configure_rejects_null(void) {
    TEST_ASSERT_FALSE(crowbar_campaign_configure(NULL));
}

static void test_configure_rejects_output_none(void) {
    crowbar_config_t c = default_cfg();
    c.output = CROWBAR_OUT_NONE;
    TEST_ASSERT_FALSE(crowbar_campaign_configure(&c));
}

static void test_configure_rejects_zero_width(void) {
    crowbar_config_t c = default_cfg();
    c.width_ns = 0u;
    TEST_ASSERT_FALSE(crowbar_campaign_configure(&c));
}

static void test_configure_rejects_width_above_max(void) {
    crowbar_config_t c = default_cfg();
    c.width_ns = CROWBAR_PIO_WIDTH_NS_MAX + 1u;
    TEST_ASSERT_FALSE(crowbar_campaign_configure(&c));
}

static void test_configure_rejects_delay_above_max(void) {
    crowbar_config_t c = default_cfg();
    c.delay_us = CROWBAR_PIO_DELAY_US_MAX + 1u;
    TEST_ASSERT_FALSE(crowbar_campaign_configure(&c));
}

static void test_configure_rejects_invalid_trigger(void) {
    crowbar_config_t c = default_cfg();
    c.trigger = (crowbar_trig_t)99;
    TEST_ASSERT_FALSE(crowbar_campaign_configure(&c));
}

static void test_configure_accepts_lp(void) {
    crowbar_config_t c = default_cfg();
    c.output = CROWBAR_OUT_LP;
    TEST_ASSERT_TRUE(crowbar_campaign_configure(&c));
}

// -----------------------------------------------------------------------------
// arm
// -----------------------------------------------------------------------------

static void test_arm_without_configure_errors(void) {
    TEST_ASSERT_FALSE(crowbar_campaign_arm());
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(CROWBAR_ERR_BAD_CONFIG, s.err);
}

static void test_arm_from_idle_goes_armed(void) {
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    TEST_ASSERT_TRUE(crowbar_campaign_arm());
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_ARMED, s.state);
}

static void test_arm_drops_both_gates_break_before_make(void) {
    // Pre-condition: driver has HP asserted (the demo cycle from
    // main.c could have left it that way before campaign took over).
    crowbar_mosfet_set_path(CROWBAR_PATH_HP);
    TEST_ASSERT_EQUAL(CROWBAR_PATH_HP, crowbar_mosfet_get_path());
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    TEST_ASSERT_TRUE(crowbar_campaign_arm());
    // After arm both gates must be LOW regardless of where they
    // started — that is what protects the SAFETY.md §3 #3 invariant
    // across the boundary.
    TEST_ASSERT_EQUAL(CROWBAR_PATH_NONE, crowbar_mosfet_get_path());
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
}

// -----------------------------------------------------------------------------
// fire
// -----------------------------------------------------------------------------

static void test_fire_from_idle_returns_false(void) {
    TEST_ASSERT_FALSE(crowbar_campaign_fire(500));
}

static void test_fire_from_armed_starts_pio_and_enters_waiting(void) {
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    TEST_ASSERT_TRUE(crowbar_campaign_fire(500));
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_WAITING, s.state);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[1].claimed);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[1].enabled);
}

static void test_fire_re_asserts_set_path_none_before_pio(void) {
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    // Operator (or a buggy callsite) toggles the gate between arm
    // and fire — fire() must squash that before handing to PIO.
    crowbar_mosfet_set_path(CROWBAR_PATH_LP);
    crowbar_campaign_fire(500);
    // The fire() call's set_path(NONE) is observable on the shadow
    // even after PIO grabs the pin (PIO writes go through hal_pio_*).
    // s_path is updated by crowbar_mosfet_set_path; PIO does not
    // touch it. So the post-fire shadow must be NONE.
    TEST_ASSERT_EQUAL(CROWBAR_PATH_NONE, crowbar_mosfet_get_path());
}

static void test_fire_pio_init_failure_enters_error(void) {
    // Force pio0/SM1 to be claimed by something else so crowbar_pio_init
    // fails — campaign must surface that as PIO_FAULT.
    hal_pio_claim_sm(hal_pio_instance(0), 1u);
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    TEST_ASSERT_FALSE(crowbar_campaign_fire(500));
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(CROWBAR_ERR_PIO_FAULT, s.err);
}

// -----------------------------------------------------------------------------
// tick (waiting → fired / error)
// -----------------------------------------------------------------------------

static void test_waiting_completes_on_pio_irq1(void) {
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    crowbar_campaign_fire(500);
    hal_fake_pio_raise_irq(0, 1);   // crowbar uses IRQ index 1
    crowbar_campaign_tick();
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_FIRED, s.state);
    TEST_ASSERT_EQUAL_UINT32(200u, s.pulse_width_ns_actual);
    TEST_ASSERT_EQUAL_UINT32(10u,  s.delay_us_actual);
    TEST_ASSERT_EQUAL(CROWBAR_OUT_HP, s.output);
}

static void test_waiting_irq0_does_not_fire(void) {
    // IRQ 0 belongs to EMFI. Crowbar tick must ignore it.
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    crowbar_campaign_fire(500);
    hal_fake_pio_raise_irq(0, 0);
    crowbar_campaign_tick();
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_WAITING, s.state);
}

static void test_waiting_times_out_when_pio_silent(void) {
    crowbar_config_t c = default_cfg();
    c.trigger = CROWBAR_TRIG_EXT_RISING;
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    crowbar_campaign_fire(50);
    advance_ms(100);
    crowbar_campaign_tick();
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(CROWBAR_ERR_TRIGGER_TIMEOUT, s.err);
}

static void test_waiting_zero_timeout_means_wait_forever(void) {
    crowbar_config_t c = default_cfg();
    c.trigger = CROWBAR_TRIG_EXT_RISING;
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    crowbar_campaign_fire(0);
    advance_ms(10000);
    crowbar_campaign_tick();
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_WAITING, s.state);
}

// -----------------------------------------------------------------------------
// disarm + recovery
// -----------------------------------------------------------------------------

static void test_disarm_returns_to_idle_and_releases_pio(void) {
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    crowbar_campaign_fire(500);
    crowbar_campaign_disarm();
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_IDLE, s.state);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[1].claimed);
}

static void test_disarm_forces_set_path_none(void) {
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    crowbar_campaign_fire(500);
    // Regardless of where the gate ended up — PIO might have left it
    // LOW after IRQ, or torn down mid-sequence — disarm guarantees
    // both gates are LOW.
    crowbar_campaign_disarm();
    TEST_ASSERT_EQUAL(CROWBAR_PATH_NONE, crowbar_mosfet_get_path());
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
}

static void test_arm_from_fired_back_to_armed(void) {
    crowbar_config_t c = default_cfg();
    crowbar_campaign_configure(&c);
    crowbar_campaign_arm();
    crowbar_campaign_fire(500);
    hal_fake_pio_raise_irq(0, 1);
    crowbar_campaign_tick();    // -> FIRED
    TEST_ASSERT_TRUE(crowbar_campaign_arm());
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_ARMED, s.state);
}

static void test_reconfigure_clears_error_state(void) {
    TEST_ASSERT_FALSE(crowbar_campaign_arm());   // -> ERROR(BAD_CONFIG)
    crowbar_config_t c = default_cfg();
    TEST_ASSERT_TRUE(crowbar_campaign_configure(&c));
    crowbar_status_t s; crowbar_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(CROWBAR_STATE_IDLE, s.state);
    TEST_ASSERT_EQUAL(CROWBAR_ERR_NONE, s.err);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_configure_rejects_null);
    RUN_TEST(test_configure_rejects_output_none);
    RUN_TEST(test_configure_rejects_zero_width);
    RUN_TEST(test_configure_rejects_width_above_max);
    RUN_TEST(test_configure_rejects_delay_above_max);
    RUN_TEST(test_configure_rejects_invalid_trigger);
    RUN_TEST(test_configure_accepts_lp);
    RUN_TEST(test_arm_without_configure_errors);
    RUN_TEST(test_arm_from_idle_goes_armed);
    RUN_TEST(test_arm_drops_both_gates_break_before_make);
    RUN_TEST(test_fire_from_idle_returns_false);
    RUN_TEST(test_fire_from_armed_starts_pio_and_enters_waiting);
    RUN_TEST(test_fire_re_asserts_set_path_none_before_pio);
    RUN_TEST(test_fire_pio_init_failure_enters_error);
    RUN_TEST(test_waiting_completes_on_pio_irq1);
    RUN_TEST(test_waiting_irq0_does_not_fire);
    RUN_TEST(test_waiting_times_out_when_pio_silent);
    RUN_TEST(test_waiting_zero_timeout_means_wait_forever);
    RUN_TEST(test_disarm_returns_to_idle_and_releases_pio);
    RUN_TEST(test_disarm_forces_set_path_none);
    RUN_TEST(test_arm_from_fired_back_to_armed);
    RUN_TEST(test_reconfigure_clears_error_state);
    return UNITY_END();
}

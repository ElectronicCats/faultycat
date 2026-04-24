// Unit tests for services/glitch_engine/emfi/emfi_campaign.

#include "unity.h"

#include "board_v2.h"
#include "emfi_campaign.h"
#include "emfi_capture.h"
#include "emfi_pio.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"
#include "hal_fake_adc.h"
#include "hal_fake_dma.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "hal_fake_pwm.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
    hal_fake_adc_reset();
    hal_fake_dma_reset();
    hal_fake_pio_reset();
    hal_fake_pwm_reset();
    emfi_capture_reset_for_test();
    hv_charger_init();
    emfi_pulse_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    emfi_campaign_init();
}
void tearDown(void) {
    emfi_campaign_disarm();
}

// Helper: fake HV charger "charged" by poking the GP18 input state.
// hv_charger_is_charged reads GP18 through hal_gpio; level=false
// means logical CHARGED because the sense line is active-low.
static void set_charged(bool on) {
    hal_fake_gpio_states[BOARD_GP_HV_CHARGED].level = !on;
}

static void advance_ms(uint32_t ms) {
    hal_fake_time_advance_ms(ms);
}

// -----------------------------------------------------------------------------

static void test_initial_state_is_idle(void) {
    emfi_status_t s;
    emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_IDLE, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_NONE, s.err);
}

static void test_configure_rejects_zero_width(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 0, .width_us = 0, .charge_timeout_ms = 1000 };
    TEST_ASSERT_FALSE(emfi_campaign_configure(&c));
}

static void test_configure_rejects_width_above_50(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 0, .width_us = 51, .charge_timeout_ms = 1000 };
    TEST_ASSERT_FALSE(emfi_campaign_configure(&c));
}

static void test_arm_without_configure_errors(void) {
    TEST_ASSERT_FALSE(emfi_campaign_arm());
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_BAD_CONFIG, s.err);
}

static void test_arm_transitions_to_arming_and_powers_hv(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    TEST_ASSERT_TRUE(emfi_campaign_arm());
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ARMING, s.state);
    TEST_ASSERT_TRUE(hv_charger_is_armed());
}

static void test_tick_promotes_arming_to_charged_on_sense(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_CHARGED, s.state);
}

static void test_charge_timeout_flips_to_error(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 100 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    // Never assert CHARGED. Default fake GPIO level is 0 which the
    // active-low sense reads as charged, so explicitly hold it high
    // (= not-charged) for the duration of the timeout.
    set_charged(false);
    advance_ms(150);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_HV_NOT_CHARGED, s.err);
    TEST_ASSERT_FALSE(hv_charger_is_armed());   // teardown disarmed.
}

static void test_fire_from_charged_enters_waiting_and_starts_pio(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    TEST_ASSERT_TRUE(emfi_campaign_fire(500));
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_WAITING, s.state);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[0].enabled);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.running);
}

static void test_hv_stale_100ms_invariant_blocks_fire(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    // Lose CHARGED and age past the window.
    set_charged(false);
    advance_ms(150);
    TEST_ASSERT_FALSE(emfi_campaign_fire(500));
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_HV_NOT_CHARGED, s.err);
}

static void test_waiting_completes_on_pio_irq0(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(500);
    // Simulate PIO finishing.
    hal_fake_pio_raise_irq(0, 0);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_FIRED, s.state);
    TEST_ASSERT_FALSE(hv_charger_is_armed());
}

static void test_waiting_times_out_if_pio_never_signals(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_EXT_RISING,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(50);
    advance_ms(100);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_ERROR, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_TRIGGER_TIMEOUT, s.err);
}

static void test_disarm_from_any_state_returns_to_idle(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(500);
    emfi_campaign_disarm();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_IDLE, s.state);
    TEST_ASSERT_FALSE(hv_charger_is_armed());
    TEST_ASSERT_FALSE(emfi_pulse_is_attached_to_pio());
}

static void test_reconfigure_clears_error_state(void) {
    // First, drive into ERROR via arm-without-configure.
    TEST_ASSERT_FALSE(emfi_campaign_arm());
    // Now configure a valid one.
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    TEST_ASSERT_TRUE(emfi_campaign_configure(&c));
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_IDLE, s.state);
    TEST_ASSERT_EQUAL(EMFI_ERR_NONE, s.err);
}

static void test_fire_records_capture_fill(void) {
    emfi_config_t c = { .trigger = EMFI_TRIG_IMMEDIATE,
                        .delay_us = 10, .width_us = 5, .charge_timeout_ms = 1000 };
    emfi_campaign_configure(&c);
    emfi_campaign_arm();
    set_charged(true);
    emfi_campaign_tick();
    emfi_campaign_fire(500);
    // Simulate DMA having written 2048 bytes by decrementing
    // transfer_count on the only claimed channel.
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) {
            hal_fake_dma_set_transfer_count(i, 0xFFFFFFFFu - 2048u);
            break;
        }
    }
    hal_fake_pio_raise_irq(0, 0);
    emfi_campaign_tick();
    emfi_status_t s; emfi_campaign_get_status(&s);
    TEST_ASSERT_EQUAL(EMFI_STATE_FIRED, s.state);
    TEST_ASSERT_EQUAL_UINT32(2048u, s.capture_fill);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_configure_rejects_zero_width);
    RUN_TEST(test_configure_rejects_width_above_50);
    RUN_TEST(test_arm_without_configure_errors);
    RUN_TEST(test_arm_transitions_to_arming_and_powers_hv);
    RUN_TEST(test_tick_promotes_arming_to_charged_on_sense);
    RUN_TEST(test_charge_timeout_flips_to_error);
    RUN_TEST(test_fire_from_charged_enters_waiting_and_starts_pio);
    RUN_TEST(test_hv_stale_100ms_invariant_blocks_fire);
    RUN_TEST(test_waiting_completes_on_pio_irq0);
    RUN_TEST(test_waiting_times_out_if_pio_never_signals);
    RUN_TEST(test_disarm_from_any_state_returns_to_idle);
    RUN_TEST(test_reconfigure_clears_error_state);
    RUN_TEST(test_fire_records_capture_fill);
    return UNITY_END();
}

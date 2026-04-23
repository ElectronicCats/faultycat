// Unit tests for drivers/ui_leds — exercised against hal_fake.

#include "unity.h"

#include "ui_leds.h"
#include "board_v2.h"
#include "hal_fake_gpio.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
}

void tearDown(void) {
}

// -----------------------------------------------------------------------------
// init
// -----------------------------------------------------------------------------

static void test_init_configures_each_pin_as_output(void) {
    ui_leds_init();
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_LED_HV_DETECTED].initialized);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_LED_STATUS].initialized);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_LED_CHARGE_ON].initialized);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_LED_HV_DETECTED].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_LED_STATUS].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_LED_CHARGE_ON].dir);
}

static void test_init_leaves_all_leds_off(void) {
    ui_leds_init();
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_LED_HV_DETECTED].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_LED_STATUS].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_LED_CHARGE_ON].level);
    TEST_ASSERT_FALSE(ui_leds_get(UI_LED_STATUS));
}

// -----------------------------------------------------------------------------
// set / get
// -----------------------------------------------------------------------------

static void test_set_drives_the_right_pin(void) {
    ui_leds_init();
    ui_leds_set(UI_LED_STATUS, true);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_LED_STATUS].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_LED_HV_DETECTED].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_LED_CHARGE_ON].level);
    TEST_ASSERT_TRUE(ui_leds_get(UI_LED_STATUS));
}

static void test_invalid_id_is_ignored(void) {
    ui_leds_init();
    ui_leds_set((ui_led_t)99, true);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_LED_STATUS].level);
    TEST_ASSERT_FALSE(ui_leds_get((ui_led_t)99));
}

// -----------------------------------------------------------------------------
// hysteresis
// -----------------------------------------------------------------------------

static void test_hv_feed_true_lights_led_immediately(void) {
    ui_leds_init();
    ui_leds_hv_detected_feed(true);
    TEST_ASSERT_TRUE(ui_leds_get(UI_LED_HV_DETECTED));
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_LED_HV_DETECTED].level);
}

static void test_hv_feed_false_within_hold_window_keeps_led_on(void) {
    ui_leds_init();
    ui_leds_hv_detected_feed(true);

    // 250 ms later — inside the 500 ms hold window.
    hal_fake_time_advance_us(250u * 1000u);
    ui_leds_hv_detected_feed(false);
    TEST_ASSERT_TRUE(ui_leds_get(UI_LED_HV_DETECTED));
}

static void test_hv_feed_false_past_hold_turns_led_off(void) {
    ui_leds_init();
    ui_leds_hv_detected_feed(true);

    // 501 ms later — beyond the 500 ms hold.
    hal_fake_time_advance_us(501u * 1000u);
    ui_leds_hv_detected_feed(false);
    TEST_ASSERT_FALSE(ui_leds_get(UI_LED_HV_DETECTED));
}

static void test_hv_refresh_resets_the_hold_timer(void) {
    ui_leds_init();
    ui_leds_hv_detected_feed(true);

    hal_fake_time_advance_us(400u * 1000u);
    ui_leds_hv_detected_feed(true);    // refresh — timer resets to now
    hal_fake_time_advance_us(400u * 1000u);
    ui_leds_hv_detected_feed(false);   // 400 ms since refresh → still inside hold
    TEST_ASSERT_TRUE(ui_leds_get(UI_LED_HV_DETECTED));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_each_pin_as_output);
    RUN_TEST(test_init_leaves_all_leds_off);
    RUN_TEST(test_set_drives_the_right_pin);
    RUN_TEST(test_invalid_id_is_ignored);
    RUN_TEST(test_hv_feed_true_lights_led_immediately);
    RUN_TEST(test_hv_feed_false_within_hold_window_keeps_led_on);
    RUN_TEST(test_hv_feed_false_past_hold_turns_led_off);
    RUN_TEST(test_hv_refresh_resets_the_hold_timer);
    return UNITY_END();
}

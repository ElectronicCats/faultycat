// Unit tests for drivers/ui_buttons — exercised against hal_fake.

#include "unity.h"

#include "ui_buttons.h"
#include "board_v2.h"
#include "hal_fake_gpio.h"

void setUp(void) {
    hal_fake_gpio_reset();
}

void tearDown(void) {
}

// -----------------------------------------------------------------------------
// init
// -----------------------------------------------------------------------------

static void test_init_configures_both_buttons_as_input(void) {
    ui_buttons_init();
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_BTN_ARM].initialized);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_BTN_PULSE].initialized);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[BOARD_GP_BTN_ARM].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[BOARD_GP_BTN_PULSE].dir);
}

static void test_init_arm_has_pulldown(void) {
    ui_buttons_init();
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_BTN_ARM].pull_up);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_BTN_ARM].pull_down);
}

static void test_init_pulse_has_pullup(void) {
    ui_buttons_init();
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_BTN_PULSE].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_BTN_PULSE].pull_down);
}

// -----------------------------------------------------------------------------
// polarity normalization — both buttons report `true` when pressed
// -----------------------------------------------------------------------------

static void test_arm_active_high_pressed_when_gpio_high(void) {
    ui_buttons_init();
    hal_fake_gpio_states[BOARD_GP_BTN_ARM].level = true;
    TEST_ASSERT_TRUE(ui_buttons_is_pressed(UI_BTN_ARM));

    hal_fake_gpio_states[BOARD_GP_BTN_ARM].level = false;
    TEST_ASSERT_FALSE(ui_buttons_is_pressed(UI_BTN_ARM));
}

static void test_pulse_active_low_pressed_when_gpio_low(void) {
    ui_buttons_init();
    hal_fake_gpio_states[BOARD_GP_BTN_PULSE].level = false;
    TEST_ASSERT_TRUE(ui_buttons_is_pressed(UI_BTN_PULSE));

    hal_fake_gpio_states[BOARD_GP_BTN_PULSE].level = true;
    TEST_ASSERT_FALSE(ui_buttons_is_pressed(UI_BTN_PULSE));
}

static void test_each_button_only_reads_its_own_pin(void) {
    ui_buttons_init();
    // Drive both pins "pressed" for their respective polarity, then
    // swap them and confirm the other doesn't bleed through.
    hal_fake_gpio_states[BOARD_GP_BTN_ARM].level   = true;
    hal_fake_gpio_states[BOARD_GP_BTN_PULSE].level = false;
    TEST_ASSERT_TRUE(ui_buttons_is_pressed(UI_BTN_ARM));
    TEST_ASSERT_TRUE(ui_buttons_is_pressed(UI_BTN_PULSE));

    hal_fake_gpio_states[BOARD_GP_BTN_ARM].level   = false;
    hal_fake_gpio_states[BOARD_GP_BTN_PULSE].level = true;
    TEST_ASSERT_FALSE(ui_buttons_is_pressed(UI_BTN_ARM));
    TEST_ASSERT_FALSE(ui_buttons_is_pressed(UI_BTN_PULSE));
}

static void test_invalid_id_returns_false(void) {
    ui_buttons_init();
    TEST_ASSERT_FALSE(ui_buttons_is_pressed((ui_btn_t)99));
    TEST_ASSERT_FALSE(ui_buttons_is_pressed(UI_BTN_COUNT));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_both_buttons_as_input);
    RUN_TEST(test_init_arm_has_pulldown);
    RUN_TEST(test_init_pulse_has_pullup);
    RUN_TEST(test_arm_active_high_pressed_when_gpio_high);
    RUN_TEST(test_pulse_active_low_pressed_when_gpio_low);
    RUN_TEST(test_each_button_only_reads_its_own_pin);
    RUN_TEST(test_invalid_id_returns_false);
    return UNITY_END();
}

// Unit tests for drivers/ext_trigger — exercised against hal_fake.

#include "unity.h"

#include "ext_trigger.h"
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

static void test_init_sets_direction_input(void) {
    ext_trigger_init(EXT_TRIGGER_PULL_NONE);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].initialized);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].dir);
}

static void test_init_with_pull_none_clears_both(void) {
    ext_trigger_init(EXT_TRIGGER_PULL_NONE);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_down);
}

static void test_init_with_pull_up(void) {
    ext_trigger_init(EXT_TRIGGER_PULL_UP);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_down);
}

static void test_init_with_pull_down(void) {
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_up);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_down);
}

// -----------------------------------------------------------------------------
// set_pull
// -----------------------------------------------------------------------------

static void test_set_pull_changes_config_without_re_init(void) {
    ext_trigger_init(EXT_TRIGGER_PULL_UP);
    uint32_t init_calls_before = hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].init_calls;

    ext_trigger_set_pull(EXT_TRIGGER_PULL_DOWN);

    TEST_ASSERT_EQUAL_UINT32(init_calls_before,
                             hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].init_calls);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_up);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].pull_down);
}

// -----------------------------------------------------------------------------
// level
// -----------------------------------------------------------------------------

static void test_level_tracks_gpio_state(void) {
    ext_trigger_init(EXT_TRIGGER_PULL_NONE);

    hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].level = false;
    TEST_ASSERT_FALSE(ext_trigger_level());

    hal_fake_gpio_states[BOARD_GP_TRIGGER_IN].level = true;
    TEST_ASSERT_TRUE(ext_trigger_level());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_sets_direction_input);
    RUN_TEST(test_init_with_pull_none_clears_both);
    RUN_TEST(test_init_with_pull_up);
    RUN_TEST(test_init_with_pull_down);
    RUN_TEST(test_set_pull_changes_config_without_re_init);
    RUN_TEST(test_level_tracks_gpio_state);
    return UNITY_END();
}

// Unit tests for hal/gpio — exercised against the host fake.

#include "unity.h"

#include "hal/gpio.h"
#include "hal_fake_gpio.h"

void setUp(void) {
    hal_fake_gpio_reset();
}

void tearDown(void) {
}

static void test_init_records_pin_direction(void) {
    hal_gpio_init(10, HAL_GPIO_DIR_OUT);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[10].initialized);
    TEST_ASSERT_EQUAL_UINT(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[10].dir);
    TEST_ASSERT_EQUAL_UINT32(1, hal_fake_gpio_states[10].init_calls);
}

static void test_put_then_get_round_trip(void) {
    hal_gpio_init(7, HAL_GPIO_DIR_OUT);
    hal_gpio_put(7, true);
    TEST_ASSERT_TRUE(hal_gpio_get(7));
    hal_gpio_put(7, false);
    TEST_ASSERT_FALSE(hal_gpio_get(7));
    TEST_ASSERT_EQUAL_UINT32(2, hal_fake_gpio_states[7].put_calls);
}

static void test_set_pulls_records_both_bits(void) {
    hal_gpio_set_pulls(11, true, false);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[11].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[11].pull_down);

    hal_gpio_set_pulls(28, false, true);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[28].pull_up);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[28].pull_down);
}

static void test_repeated_init_counts_calls(void) {
    hal_gpio_init(1, HAL_GPIO_DIR_IN);
    hal_gpio_init(1, HAL_GPIO_DIR_OUT);
    TEST_ASSERT_EQUAL_UINT32(2, hal_fake_gpio_states[1].init_calls);
    // The last-written direction wins.
    TEST_ASSERT_EQUAL_UINT(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[1].dir);
}

static void test_pin_out_of_range_is_a_noop(void) {
    hal_gpio_init(HAL_FAKE_GPIO_MAX_PINS, HAL_GPIO_DIR_OUT);
    hal_gpio_put(HAL_FAKE_GPIO_MAX_PINS + 10, true);
    TEST_ASSERT_FALSE(hal_gpio_get(HAL_FAKE_GPIO_MAX_PINS));
    // Nothing should have been recorded for valid pins.
    for (uint8_t p = 0; p < HAL_FAKE_GPIO_MAX_PINS; p++) {
        TEST_ASSERT_FALSE(hal_fake_gpio_states[p].initialized);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_records_pin_direction);
    RUN_TEST(test_put_then_get_round_trip);
    RUN_TEST(test_set_pulls_records_both_bits);
    RUN_TEST(test_repeated_init_counts_calls);
    RUN_TEST(test_pin_out_of_range_is_a_noop);
    return UNITY_END();
}

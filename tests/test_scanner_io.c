// Unit tests for drivers/scanner_io — exercised against hal_fake.

#include "unity.h"

#include "scanner_io.h"
#include "board_v2.h"
#include "hal_fake_gpio.h"

static const uint8_t EXPECTED_PIN[SCANNER_IO_CHANNEL_COUNT] = {
    BOARD_GP_SCANNER_CH0,
    BOARD_GP_SCANNER_CH1,
    BOARD_GP_SCANNER_CH2,
    BOARD_GP_SCANNER_CH3,
    BOARD_GP_SCANNER_CH4,
    BOARD_GP_SCANNER_CH5,
    BOARD_GP_SCANNER_CH6,
    BOARD_GP_SCANNER_CH7,
};

void setUp(void) {
    hal_fake_gpio_reset();
}

void tearDown(void) {
}

// -----------------------------------------------------------------------------
// init
// -----------------------------------------------------------------------------

static void test_init_configures_all_8_as_input(void) {
    scanner_io_init();
    for (unsigned ch = 0; ch < SCANNER_IO_CHANNEL_COUNT; ch++) {
        uint8_t pin = EXPECTED_PIN[ch];
        TEST_ASSERT_TRUE(hal_fake_gpio_states[pin].initialized);
        TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[pin].dir);
    }
}

static void test_init_enables_pullup_on_all_channels(void) {
    scanner_io_init();
    for (unsigned ch = 0; ch < SCANNER_IO_CHANNEL_COUNT; ch++) {
        uint8_t pin = EXPECTED_PIN[ch];
        TEST_ASSERT_TRUE(hal_fake_gpio_states[pin].pull_up);
        TEST_ASSERT_FALSE(hal_fake_gpio_states[pin].pull_down);
    }
}

// -----------------------------------------------------------------------------
// set_dir / put / get
// -----------------------------------------------------------------------------

static void test_set_dir_out_switches_direction(void) {
    scanner_io_init();
    scanner_io_set_dir(3, SCANNER_IO_DIR_OUT);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[EXPECTED_PIN[3]].dir);
}

static void test_put_and_get_round_trip(void) {
    scanner_io_init();
    scanner_io_set_dir(5, SCANNER_IO_DIR_OUT);
    scanner_io_put(5, true);
    TEST_ASSERT_TRUE(scanner_io_get(5));
    scanner_io_put(5, false);
    TEST_ASSERT_FALSE(scanner_io_get(5));
}

static void test_out_of_range_is_safe(void) {
    scanner_io_init();
    scanner_io_set_dir(99, SCANNER_IO_DIR_OUT);
    scanner_io_put(99, true);
    TEST_ASSERT_FALSE(scanner_io_get(99));
    TEST_ASSERT_FALSE(scanner_io_get(SCANNER_IO_CHANNEL_COUNT));
}

// -----------------------------------------------------------------------------
// read_all
// -----------------------------------------------------------------------------

static void test_read_all_zero_when_all_low(void) {
    scanner_io_init();
    for (unsigned ch = 0; ch < SCANNER_IO_CHANNEL_COUNT; ch++) {
        hal_fake_gpio_states[EXPECTED_PIN[ch]].level = false;
    }
    TEST_ASSERT_EQUAL_HEX8(0x00, scanner_io_read_all());
}

static void test_read_all_ones_when_all_high(void) {
    scanner_io_init();
    for (unsigned ch = 0; ch < SCANNER_IO_CHANNEL_COUNT; ch++) {
        hal_fake_gpio_states[EXPECTED_PIN[ch]].level = true;
    }
    TEST_ASSERT_EQUAL_HEX8(0xFF, scanner_io_read_all());
}

static void test_read_all_packs_channels_in_lsb_order(void) {
    scanner_io_init();
    // Pattern: only CH0 and CH3 are high → 0b00001001 = 0x09
    hal_fake_gpio_states[EXPECTED_PIN[0]].level = true;
    hal_fake_gpio_states[EXPECTED_PIN[3]].level = true;
    TEST_ASSERT_EQUAL_HEX8(0x09, scanner_io_read_all());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_all_8_as_input);
    RUN_TEST(test_init_enables_pullup_on_all_channels);
    RUN_TEST(test_set_dir_out_switches_direction);
    RUN_TEST(test_put_and_get_round_trip);
    RUN_TEST(test_out_of_range_is_safe);
    RUN_TEST(test_read_all_zero_when_all_low);
    RUN_TEST(test_read_all_ones_when_all_high);
    RUN_TEST(test_read_all_packs_channels_in_lsb_order);
    return UNITY_END();
}

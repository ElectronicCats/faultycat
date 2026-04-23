// Unit tests for drivers/target_monitor — exercised against hal_fake.

#include "unity.h"

#include "target_monitor.h"
#include "board_v2.h"
#include "hal_fake_adc.h"

void setUp(void) {
    hal_fake_adc_reset();
}

void tearDown(void) {
}

static void test_init_calls_hal_adc_init_once(void) {
    target_monitor_init();
    TEST_ASSERT_TRUE(hal_fake_adc_initialized);
    TEST_ASSERT_EQUAL_UINT32(1, hal_fake_adc_init_calls);
}

static void test_init_enables_only_our_channel(void) {
    target_monitor_init();
    TEST_ASSERT_TRUE(hal_fake_adc_channels[BOARD_TARGET_ADC_CHANNEL].enabled);
    TEST_ASSERT_EQUAL_UINT32(1, hal_fake_adc_channels[BOARD_TARGET_ADC_CHANNEL].enable_calls);

    for (uint8_t ch = 0; ch < HAL_FAKE_ADC_MAX_CHANNELS; ch++) {
        if (ch == BOARD_TARGET_ADC_CHANNEL) continue;
        TEST_ASSERT_FALSE(hal_fake_adc_channels[ch].enabled);
    }
}

static void test_read_returns_simulated_value(void) {
    target_monitor_init();
    hal_fake_adc_set_value(BOARD_TARGET_ADC_CHANNEL, 1234);
    TEST_ASSERT_EQUAL_UINT16(1234, target_monitor_read_raw());
}

static void test_read_updates_when_value_changes(void) {
    target_monitor_init();
    hal_fake_adc_set_value(BOARD_TARGET_ADC_CHANNEL, 0);
    TEST_ASSERT_EQUAL_UINT16(0, target_monitor_read_raw());

    hal_fake_adc_set_value(BOARD_TARGET_ADC_CHANNEL, 4095);
    TEST_ASSERT_EQUAL_UINT16(4095, target_monitor_read_raw());
}

static void test_read_only_reads_target_channel(void) {
    target_monitor_init();
    hal_fake_adc_set_value(BOARD_TARGET_ADC_CHANNEL, 2048);
    target_monitor_read_raw();
    target_monitor_read_raw();

    TEST_ASSERT_EQUAL_UINT32(2, hal_fake_adc_channels[BOARD_TARGET_ADC_CHANNEL].read_calls);
    for (uint8_t ch = 0; ch < HAL_FAKE_ADC_MAX_CHANNELS; ch++) {
        if (ch == BOARD_TARGET_ADC_CHANNEL) continue;
        TEST_ASSERT_EQUAL_UINT32(0, hal_fake_adc_channels[ch].read_calls);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_calls_hal_adc_init_once);
    RUN_TEST(test_init_enables_only_our_channel);
    RUN_TEST(test_read_returns_simulated_value);
    RUN_TEST(test_read_updates_when_value_changes);
    RUN_TEST(test_read_only_reads_target_channel);
    return UNITY_END();
}

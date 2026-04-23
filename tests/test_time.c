// Unit tests for hal/time — exercised against the host fake.

#include "unity.h"

#include "hal/time.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_time_reset();
}

void tearDown(void) {
}

static void test_clock_starts_at_zero(void) {
    TEST_ASSERT_EQUAL_UINT32(0, hal_now_ms());
    TEST_ASSERT_EQUAL_UINT32(0, hal_now_us());
}

static void test_sleep_ms_advances_clock(void) {
    hal_sleep_ms(500);
    TEST_ASSERT_EQUAL_UINT32(500, hal_now_ms());
    TEST_ASSERT_EQUAL_UINT32(500000, hal_now_us());
}

static void test_sleep_zero_is_noop(void) {
    hal_sleep_ms(0);
    TEST_ASSERT_EQUAL_UINT32(0, hal_now_ms());
    TEST_ASSERT_EQUAL_UINT32(0, hal_now_us());
}

static void test_sleeps_are_cumulative(void) {
    hal_sleep_ms(100);
    uint32_t t1 = hal_now_ms();
    hal_sleep_ms(250);
    uint32_t t2 = hal_now_ms();
    TEST_ASSERT_EQUAL_UINT32(100, t1);
    TEST_ASSERT_EQUAL_UINT32(350, t2);
    TEST_ASSERT_GREATER_THAN_UINT32(t1, t2);
}

static void test_explicit_us_advance(void) {
    hal_fake_time_advance_us(2500);
    TEST_ASSERT_EQUAL_UINT32(2500, hal_now_us());
    TEST_ASSERT_EQUAL_UINT32(2, hal_now_ms());   // 2500us = 2ms truncated
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clock_starts_at_zero);
    RUN_TEST(test_sleep_ms_advances_clock);
    RUN_TEST(test_sleep_zero_is_noop);
    RUN_TEST(test_sleeps_are_cumulative);
    RUN_TEST(test_explicit_us_advance);
    return UNITY_END();
}

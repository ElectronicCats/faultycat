// hal/time — native host fake. A 64-bit microsecond counter that only
// moves when hal_sleep_ms() is called or the test explicitly advances it.

#include "hal/time.h"
#include "hal_fake_time.h"

static uint64_t s_fake_now_us;

void hal_fake_time_reset(void) {
    s_fake_now_us = 0;
}

void hal_fake_time_advance_us(uint32_t us) {
    s_fake_now_us += us;
}

uint64_t hal_fake_time_now_us_u64(void) {
    return s_fake_now_us;
}

void hal_sleep_ms(uint32_t ms) {
    if (ms == 0) {
        return;
    }
    s_fake_now_us += (uint64_t)ms * 1000u;
}

uint32_t hal_now_ms(void) {
    return (uint32_t)(s_fake_now_us / 1000u);
}

uint32_t hal_now_us(void) {
    return (uint32_t)s_fake_now_us;
}

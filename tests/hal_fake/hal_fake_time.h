#pragma once

#include <stdint.h>

// Fake clock control. The fake starts at 0 and only advances when
// hal_sleep_ms() is called or when the test explicitly advances it
// via hal_fake_time_advance_us().

void hal_fake_time_reset(void);

// Advance the fake clock by N microseconds. Useful for simulating
// external wall-clock progress without blocking.
void hal_fake_time_advance_us(uint32_t us);

// Read the 64-bit microsecond counter. Use hal_now_ms/hal_now_us for
// the truncated 32-bit views that real code sees.
uint64_t hal_fake_time_now_us_u64(void);

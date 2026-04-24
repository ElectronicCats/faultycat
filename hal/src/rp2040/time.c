// hal/time — RP2040 implementation. Wraps pico-sdk's pico_time.

#include "hal/time.h"

#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/time.h"

void hal_sleep_ms(uint32_t ms) {
    if (ms == 0) {
        return;
    }
    sleep_ms(ms);
}

uint32_t hal_now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

uint32_t hal_now_us(void) {
    return time_us_32();
}

void hal_busy_wait_us(uint32_t us) {
    busy_wait_us_32(us);
}

uint32_t hal_irq_save_and_disable(void) {
    return save_and_disable_interrupts();
}

void hal_irq_restore(uint32_t cookie) {
    restore_interrupts(cookie);
}

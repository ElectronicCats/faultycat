#include "emfi_pulse.h"

#include <stdbool.h>

#include "board_v2.h"
#include "hal/gpio.h"
#include "hal/time.h"

void emfi_pulse_init(void) {
    // GP14 as plain GPIO output, driven low. F4 will hand ownership
    // of this pin to PIO for triggered fires; until then, CPU-timed
    // fires are what we support.
    hal_gpio_init(BOARD_GP_HV_PULSE, HAL_GPIO_DIR_OUT);
    hal_gpio_set_pulls(BOARD_GP_HV_PULSE, false, false);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
}

void emfi_pulse_force_low(void) {
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
}

bool emfi_pulse_fire_manual(uint32_t width_us) {
    if (width_us < EMFI_PULSE_MIN_WIDTH_US
     || width_us > EMFI_PULSE_MAX_WIDTH_US) {
        return false;
    }

    // Critical section: disable interrupts so the busy-wait cannot be
    // stretched by a USB or timer IRQ. Ported from legacy picoemp.c.
    uint32_t cookie = hal_irq_save_and_disable();
    hal_gpio_put(BOARD_GP_HV_PULSE, true);
    hal_busy_wait_us(width_us);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
    hal_irq_restore(cookie);

    // Post-fire cool-down. Outside the critical section — interrupts
    // are allowed again so stdio/USB can service during the sleep.
    hal_sleep_ms(EMFI_PULSE_COOLDOWN_MS);
    return true;
}

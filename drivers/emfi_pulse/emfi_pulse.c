#include "emfi_pulse.h"

#include <stdbool.h>

#include "board_v2.h"
#include "hal/gpio.h"
#include "hal/pio.h"
#include "hal/time.h"

static bool             s_attached = false;
static hal_pio_inst_t  *s_pio      = NULL;
static uint32_t         s_sm       = 0;

void emfi_pulse_init(void) {
    hal_gpio_init(BOARD_GP_HV_PULSE, HAL_GPIO_DIR_OUT);
    hal_gpio_set_pulls(BOARD_GP_HV_PULSE, false, false);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
    s_attached = false;
    s_pio      = NULL;
    s_sm       = 0;
}

void emfi_pulse_force_low(void) {
    // Force implies "back to CPU-owned LOW". If a PIO was attached it
    // no longer is; F4-5 layer is responsible for also tearing down
    // the SM; this driver only owns the pin.
    if (s_attached) {
        hal_gpio_init(BOARD_GP_HV_PULSE, HAL_GPIO_DIR_OUT);
        s_attached = false;
        s_pio      = NULL;
    }
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
}

bool emfi_pulse_fire_manual(uint32_t width_us) {
    if (s_attached) {
        return false;
    }
    if (width_us < EMFI_PULSE_MIN_WIDTH_US
     || width_us > EMFI_PULSE_MAX_WIDTH_US) {
        return false;
    }
    uint32_t cookie = hal_irq_save_and_disable();
    hal_gpio_put(BOARD_GP_HV_PULSE, true);
    hal_busy_wait_us(width_us);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
    hal_irq_restore(cookie);
    hal_sleep_ms(EMFI_PULSE_COOLDOWN_MS);
    return true;
}

bool emfi_pulse_attach_pio(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || s_attached) {
        return false;
    }
    s_pio      = pio;
    s_sm       = sm;
    s_attached = true;
    return true;
}

void emfi_pulse_detach_pio(void) {
    if (!s_attached) {
        return;
    }
    s_attached = false;
    s_pio      = NULL;
    // Return pin to plain GPIO LOW.
    hal_gpio_init(BOARD_GP_HV_PULSE, HAL_GPIO_DIR_OUT);
    hal_gpio_put(BOARD_GP_HV_PULSE, false);
}

bool emfi_pulse_is_attached_to_pio(void) {
    return s_attached;
}

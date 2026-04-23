// hal/gpio — native host fake. Implements hal_gpio_* by recording
// every call into per-pin state that tests inspect via
// hal_fake_gpio_states[pin].

#include "hal_fake_gpio.h"

#include <string.h>

hal_fake_gpio_state_t hal_fake_gpio_states[HAL_FAKE_GPIO_MAX_PINS];

void hal_fake_gpio_reset(void) {
    memset(hal_fake_gpio_states, 0, sizeof(hal_fake_gpio_states));
}

void hal_gpio_init(hal_gpio_pin_t pin, hal_gpio_dir_t dir) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return;
    }
    hal_fake_gpio_state_t *s = &hal_fake_gpio_states[pin];
    s->initialized = true;
    s->dir         = dir;
    s->init_calls++;
}

void hal_gpio_put(hal_gpio_pin_t pin, bool value) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return;
    }
    hal_fake_gpio_states[pin].level = value;
    hal_fake_gpio_states[pin].put_calls++;
}

bool hal_gpio_get(hal_gpio_pin_t pin) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return false;
    }
    hal_fake_gpio_states[pin].get_calls++;
    return hal_fake_gpio_states[pin].level;
}

void hal_gpio_set_pulls(hal_gpio_pin_t pin, bool pull_up, bool pull_down) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return;
    }
    hal_fake_gpio_state_t *s = &hal_fake_gpio_states[pin];
    s->pull_up   = pull_up;
    s->pull_down = pull_down;
    s->pulls_calls++;
}

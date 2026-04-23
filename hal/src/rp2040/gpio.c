// hal/gpio — RP2040 implementation. Thin wrapper over pico-sdk's
// hardware_gpio library. Every call passes straight through; the HAL
// exists so tests and drivers don't need to know which MCU they run on.

#include "hal/gpio.h"

#include "hardware/gpio.h"

void hal_gpio_init(hal_gpio_pin_t pin, hal_gpio_dir_t dir) {
    gpio_init(pin);
    gpio_set_dir(pin, dir == HAL_GPIO_DIR_OUT);
}

void hal_gpio_put(hal_gpio_pin_t pin, bool value) {
    gpio_put(pin, value);
}

bool hal_gpio_get(hal_gpio_pin_t pin) {
    return gpio_get(pin);
}

void hal_gpio_set_pulls(hal_gpio_pin_t pin, bool pull_up, bool pull_down) {
    gpio_set_pulls(pin, pull_up, pull_down);
}

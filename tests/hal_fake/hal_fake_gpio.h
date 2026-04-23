#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/gpio.h"

// Number of pins the fake tracks. RP2040 has 30 GPIOs; we round up
// to 32 so anyone stores full-byte arrays if they want to.
#define HAL_FAKE_GPIO_MAX_PINS 32

typedef struct {
    bool            initialized;
    hal_gpio_dir_t  dir;
    bool            level;
    bool            pull_up;
    bool            pull_down;
    uint32_t        init_calls;
    uint32_t        put_calls;
    uint32_t        get_calls;
    uint32_t        pulls_calls;
} hal_fake_gpio_state_t;

extern hal_fake_gpio_state_t hal_fake_gpio_states[HAL_FAKE_GPIO_MAX_PINS];

// Zero every pin's state. Tests should call this from setUp().
void hal_fake_gpio_reset(void);

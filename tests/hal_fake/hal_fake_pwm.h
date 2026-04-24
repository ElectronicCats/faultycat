#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/pwm.h"

#define HAL_FAKE_PWM_MAX_PINS 32

typedef struct {
    bool     initialized;
    float    last_freq;
    float    last_duty;
    bool     enabled;
    uint32_t init_calls;
    uint32_t set_calls;
    uint32_t enable_calls;
    uint32_t disable_calls;
} hal_fake_pwm_state_t;

extern hal_fake_pwm_state_t hal_fake_pwm_states[HAL_FAKE_PWM_MAX_PINS];

void hal_fake_pwm_reset(void);

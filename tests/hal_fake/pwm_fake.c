#include "hal_fake_pwm.h"

#include <string.h>

hal_fake_pwm_state_t hal_fake_pwm_states[HAL_FAKE_PWM_MAX_PINS];

void hal_fake_pwm_reset(void) {
    memset(hal_fake_pwm_states, 0, sizeof(hal_fake_pwm_states));
}

void hal_pwm_init(hal_pwm_pin_t pin) {
    if (pin >= HAL_FAKE_PWM_MAX_PINS) return;
    hal_fake_pwm_states[pin].initialized = true;
    hal_fake_pwm_states[pin].init_calls++;
    // init implicitly disables the waveform until enable is called.
    hal_fake_pwm_states[pin].enabled = false;
}

void hal_pwm_set_freq_duty(hal_pwm_pin_t pin, float hz, float duty) {
    if (pin >= HAL_FAKE_PWM_MAX_PINS) return;
    hal_fake_pwm_states[pin].last_freq = hz;
    hal_fake_pwm_states[pin].last_duty = duty;
    hal_fake_pwm_states[pin].set_calls++;
}

void hal_pwm_enable(hal_pwm_pin_t pin) {
    if (pin >= HAL_FAKE_PWM_MAX_PINS) return;
    hal_fake_pwm_states[pin].enabled = true;
    hal_fake_pwm_states[pin].enable_calls++;
}

void hal_pwm_disable(hal_pwm_pin_t pin) {
    if (pin >= HAL_FAKE_PWM_MAX_PINS) return;
    hal_fake_pwm_states[pin].enabled = false;
    hal_fake_pwm_states[pin].disable_calls++;
}

bool hal_pwm_is_enabled(hal_pwm_pin_t pin) {
    if (pin >= HAL_FAKE_PWM_MAX_PINS) return false;
    return hal_fake_pwm_states[pin].enabled;
}

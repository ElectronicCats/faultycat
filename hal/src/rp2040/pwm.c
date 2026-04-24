#include "hal/pwm.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

// Tracks PWM-enabled state per slice so hal_pwm_is_enabled can answer
// without querying hardware registers (they don't expose a clean
// "enabled" bit).
static uint8_t s_slice_enabled_mask;  // RP2040 has 8 PWM slices

static inline uint slice_of(hal_pwm_pin_t pin) {
    return pwm_gpio_to_slice_num(pin);
}

static inline uint chan_of(hal_pwm_pin_t pin) {
    return pwm_gpio_to_channel(pin);
}

void hal_pwm_init(hal_pwm_pin_t pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = slice_of(pin);
    pwm_config cfg = pwm_get_default_config();
    pwm_init(slice, &cfg, false);
    s_slice_enabled_mask &= (uint8_t)~(1u << slice);
}

void hal_pwm_set_freq_duty(hal_pwm_pin_t pin, float hz, float duty) {
    if (hz <= 0.0f) {
        return;
    }
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;

    uint slice = slice_of(pin);
    uint chan  = chan_of(pin);

    uint32_t clock = clock_get_hz(clk_sys);

    // Ported from firmware/c/picoemp.c: divider16/16 + wrap solves for
    // the requested frequency within the clock's resolution.
    uint32_t divider16 = (uint32_t)((float)clock / hz / 4096.0f)
                         + ((clock % (uint32_t)(hz * 4096.0f)) != 0 ? 1u : 0u);
    if (divider16 / 16u == 0u) {
        divider16 = 16u;
    }
    uint32_t wrap = (uint32_t)((float)clock * 16.0f / (float)divider16 / hz) - 1u;

    pwm_set_clkdiv_int_frac(slice, (uint8_t)(divider16 / 16u),
                            (uint8_t)(divider16 & 0xFu));
    pwm_set_wrap(slice, (uint16_t)wrap);
    pwm_set_chan_level(slice, chan, (uint16_t)((float)wrap * duty));
}

void hal_pwm_enable(hal_pwm_pin_t pin) {
    uint slice = slice_of(pin);
    pwm_set_enabled(slice, true);
    s_slice_enabled_mask |= (uint8_t)(1u << slice);
}

void hal_pwm_disable(hal_pwm_pin_t pin) {
    uint slice = slice_of(pin);
    pwm_set_enabled(slice, false);
    s_slice_enabled_mask &= (uint8_t)~(1u << slice);
}

bool hal_pwm_is_enabled(hal_pwm_pin_t pin) {
    uint slice = slice_of(pin);
    return (s_slice_enabled_mask & (1u << slice)) != 0u;
}

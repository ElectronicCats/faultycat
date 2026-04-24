#include "hv_charger.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_v2.h"
#include "hal/gpio.h"
#include "hal/pwm.h"
#include "hal/time.h"

#define HV_CHARGER_DEFAULT_FREQ_HZ       2500.0f
#define HV_CHARGER_DEFAULT_DUTY          0.0122f
#define HV_CHARGER_DEFAULT_AUTODISARM_MS 60000u

static hv_charger_config_t s_cfg = {
    .flyback_freq_hz = HV_CHARGER_DEFAULT_FREQ_HZ,
    .flyback_duty    = HV_CHARGER_DEFAULT_DUTY,
    .auto_disarm_ms  = HV_CHARGER_DEFAULT_AUTODISARM_MS,
};

static bool     s_armed;
static uint32_t s_armed_at_ms;

static void force_pwm_off_and_pin_low(void) {
    // Disable PWM, then re-claim GP20 as a plain output and drive it
    // LOW. This avoids leaving the flyback FET at an undefined level
    // during the handover between PWM peripheral and GPIO control.
    hal_pwm_disable(BOARD_GP_HV_PWM);
    hal_gpio_init(BOARD_GP_HV_PWM, HAL_GPIO_DIR_OUT);
    hal_gpio_put(BOARD_GP_HV_PWM, false);
}

void hv_charger_init(void) {
    // Feedback pin first — read-only, no risk.
    hal_gpio_init(BOARD_GP_HV_CHARGED, HAL_GPIO_DIR_IN);
    hal_gpio_set_pulls(BOARD_GP_HV_CHARGED, false, false);

    // Default config.
    s_cfg.flyback_freq_hz = HV_CHARGER_DEFAULT_FREQ_HZ;
    s_cfg.flyback_duty    = HV_CHARGER_DEFAULT_DUTY;
    s_cfg.auto_disarm_ms  = HV_CHARGER_DEFAULT_AUTODISARM_MS;

    // HV drive pin starts DISARMED: plain GPIO output at LOW.
    force_pwm_off_and_pin_low();

    s_armed       = false;
    s_armed_at_ms = 0;
}

void hv_charger_configure(const hv_charger_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }
    s_cfg = *cfg;
}

void hv_charger_get_config(hv_charger_config_t *out) {
    if (out == NULL) {
        return;
    }
    *out = s_cfg;
}

void hv_charger_arm(void) {
    // Configure PWM with current settings, then enable. Switching
    // the pin's function from plain GPIO to PWM happens inside
    // hal_pwm_init.
    hal_pwm_init(BOARD_GP_HV_PWM);
    hal_pwm_set_freq_duty(BOARD_GP_HV_PWM, s_cfg.flyback_freq_hz,
                          s_cfg.flyback_duty);
    hal_pwm_enable(BOARD_GP_HV_PWM);

    s_armed       = true;
    s_armed_at_ms = hal_now_ms();
}

void hv_charger_disarm(void) {
    force_pwm_off_and_pin_low();
    s_armed = false;
}

bool hv_charger_is_armed(void) {
    return s_armed;
}

bool hv_charger_is_charged(void) {
    // GP18 is active-low: the signal asserts LOW when the cap is
    // charged. Return the normalized (active-high) logical state.
    return hal_gpio_get(BOARD_GP_HV_CHARGED) == false;
}

void hv_charger_tick(void) {
    if (!s_armed) {
        return;
    }
    if (s_cfg.auto_disarm_ms == 0u) {
        // Timeout explicitly disabled — stay armed until caller
        // disarms.
        return;
    }
    uint32_t now = hal_now_ms();
    // Wrap-safe subtraction: hal_now_ms is a 32-bit monotonic
    // counter that wraps every 49.7 days. Auto-disarm windows are
    // always well below that horizon.
    if ((uint32_t)(now - s_armed_at_ms) >= s_cfg.auto_disarm_ms) {
        hv_charger_disarm();
    }
}

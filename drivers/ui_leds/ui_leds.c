#include "ui_leds.h"

#include "board_v2.h"
#include "hal/gpio.h"
#include "hal/time.h"

static const uint8_t s_pin[UI_LED_COUNT] = {
    [UI_LED_HV_DETECTED] = BOARD_GP_LED_HV_DETECTED,
    [UI_LED_STATUS]      = BOARD_GP_LED_STATUS,
    [UI_LED_CHARGE_ON]   = BOARD_GP_LED_CHARGE_ON,
};

static bool     s_on[UI_LED_COUNT];
static uint32_t s_last_charged_ms;

void ui_leds_init(void) {
    for (unsigned i = 0; i < UI_LED_COUNT; i++) {
        hal_gpio_init(s_pin[i], HAL_GPIO_DIR_OUT);
        hal_gpio_put(s_pin[i], false);
        s_on[i] = false;
    }
    s_last_charged_ms = 0;
}

void ui_leds_set(ui_led_t led, bool on) {
    if ((unsigned)led >= UI_LED_COUNT) {
        return;
    }
    hal_gpio_put(s_pin[led], on);
    s_on[led] = on;
}

bool ui_leds_get(ui_led_t led) {
    if ((unsigned)led >= UI_LED_COUNT) {
        return false;
    }
    return s_on[led];
}

void ui_leds_hv_detected_feed(bool charged_now) {
    uint32_t now_ms = hal_now_ms();

    if (charged_now) {
        s_last_charged_ms = now_ms;
        ui_leds_set(UI_LED_HV_DETECTED, true);
        return;
    }

    // Wrap-safe elapsed comparison — holds up to 49 days after which
    // the 32-bit clock wraps, which matches hal_now_ms's documented
    // range. Services that need longer horizons must use a 64-bit
    // time API when we add one.
    if ((now_ms - s_last_charged_ms) > UI_LEDS_HV_HOLD_MS) {
        ui_leds_set(UI_LED_HV_DETECTED, false);
    }
}

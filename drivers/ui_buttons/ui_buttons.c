#include "ui_buttons.h"

#include <stdint.h>

#include "board_v2.h"
#include "hal/gpio.h"

typedef struct {
    uint8_t pin;
    bool    pull_up;
    bool    pull_down;
    bool    pressed_level;  // the logical pin level that means "pressed"
} ui_button_config_t;

static const ui_button_config_t s_cfg[UI_BTN_COUNT] = {
    [UI_BTN_ARM] = {
        .pin            = BOARD_GP_BTN_ARM,
        .pull_up        = false,
        .pull_down      = true,
        .pressed_level  = true,    // active-high
    },
    [UI_BTN_PULSE] = {
        .pin            = BOARD_GP_BTN_PULSE,
        .pull_up        = true,
        .pull_down      = false,
        .pressed_level  = false,   // active-low
    },
};

void ui_buttons_init(void) {
    for (unsigned i = 0; i < UI_BTN_COUNT; i++) {
        hal_gpio_init(s_cfg[i].pin, HAL_GPIO_DIR_IN);
        hal_gpio_set_pulls(s_cfg[i].pin, s_cfg[i].pull_up, s_cfg[i].pull_down);
    }
}

bool ui_buttons_is_pressed(ui_btn_t btn) {
    if ((unsigned)btn >= UI_BTN_COUNT) {
        return false;
    }
    return hal_gpio_get(s_cfg[btn].pin) == s_cfg[btn].pressed_level;
}

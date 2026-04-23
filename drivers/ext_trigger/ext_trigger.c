#include "ext_trigger.h"

#include "board_v2.h"
#include "hal/gpio.h"

static void apply_pull(ext_trigger_pull_t pull) {
    bool up   = (pull == EXT_TRIGGER_PULL_UP);
    bool down = (pull == EXT_TRIGGER_PULL_DOWN);
    hal_gpio_set_pulls(BOARD_GP_TRIGGER_IN, up, down);
}

void ext_trigger_init(ext_trigger_pull_t pull) {
    hal_gpio_init(BOARD_GP_TRIGGER_IN, HAL_GPIO_DIR_IN);
    apply_pull(pull);
}

void ext_trigger_set_pull(ext_trigger_pull_t pull) {
    apply_pull(pull);
}

bool ext_trigger_level(void) {
    return hal_gpio_get(BOARD_GP_TRIGGER_IN);
}

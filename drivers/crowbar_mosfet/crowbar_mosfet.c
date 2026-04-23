#include "crowbar_mosfet.h"

#include "board_v2.h"
#include "hal/gpio.h"

static crowbar_path_t s_path = CROWBAR_PATH_NONE;

static void drive_both_low(void) {
    // Break phase of break-before-make. Explicitly re-asserts both
    // gates LOW regardless of requested target. Cheap — 2 GPIO writes.
    hal_gpio_put(BOARD_GP_CROWBAR_LP, false);
    hal_gpio_put(BOARD_GP_CROWBAR_HP, false);
}

void crowbar_mosfet_init(void) {
    hal_gpio_init(BOARD_GP_CROWBAR_LP, HAL_GPIO_DIR_OUT);
    hal_gpio_init(BOARD_GP_CROWBAR_HP, HAL_GPIO_DIR_OUT);
    drive_both_low();
    s_path = CROWBAR_PATH_NONE;
}

void crowbar_mosfet_set_path(crowbar_path_t path) {
    // Break phase — always drop both gates LOW first, unconditionally.
    drive_both_low();

    // Make phase — raise exactly one gate, or none.
    switch (path) {
        case CROWBAR_PATH_LP:
            hal_gpio_put(BOARD_GP_CROWBAR_LP, true);
            s_path = CROWBAR_PATH_LP;
            break;
        case CROWBAR_PATH_HP:
            hal_gpio_put(BOARD_GP_CROWBAR_HP, true);
            s_path = CROWBAR_PATH_HP;
            break;
        case CROWBAR_PATH_NONE:
        default:
            // Out-of-range enum values fall through here by design —
            // unknown input means "go safe".
            s_path = CROWBAR_PATH_NONE;
            break;
    }
}

crowbar_path_t crowbar_mosfet_get_path(void) {
    return s_path;
}

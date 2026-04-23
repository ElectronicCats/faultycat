#include "scanner_io.h"

#include "board_v2.h"
#include "hal/gpio.h"

static const uint8_t s_pin[SCANNER_IO_CHANNEL_COUNT] = {
    BOARD_GP_SCANNER_CH0,
    BOARD_GP_SCANNER_CH1,
    BOARD_GP_SCANNER_CH2,
    BOARD_GP_SCANNER_CH3,
    BOARD_GP_SCANNER_CH4,
    BOARD_GP_SCANNER_CH5,
    BOARD_GP_SCANNER_CH6,
    BOARD_GP_SCANNER_CH7,
};

void scanner_io_init(void) {
    for (unsigned i = 0; i < SCANNER_IO_CHANNEL_COUNT; i++) {
        hal_gpio_init(s_pin[i], HAL_GPIO_DIR_IN);
        hal_gpio_set_pulls(s_pin[i], true, false);
    }
}

void scanner_io_set_dir(uint8_t channel, scanner_io_dir_t dir) {
    if (channel >= SCANNER_IO_CHANNEL_COUNT) {
        return;
    }
    hal_gpio_init(s_pin[channel], dir == SCANNER_IO_DIR_OUT ? HAL_GPIO_DIR_OUT
                                                            : HAL_GPIO_DIR_IN);
}

void scanner_io_put(uint8_t channel, bool value) {
    if (channel >= SCANNER_IO_CHANNEL_COUNT) {
        return;
    }
    hal_gpio_put(s_pin[channel], value);
}

bool scanner_io_get(uint8_t channel) {
    if (channel >= SCANNER_IO_CHANNEL_COUNT) {
        return false;
    }
    return hal_gpio_get(s_pin[channel]);
}

uint8_t scanner_io_read_all(void) {
    uint8_t bits = 0;
    for (unsigned i = 0; i < SCANNER_IO_CHANNEL_COUNT; i++) {
        if (hal_gpio_get(s_pin[i])) {
            bits |= (uint8_t)(1u << i);
        }
    }
    return bits;
}

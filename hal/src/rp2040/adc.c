#include "hal/adc.h"

#include "hardware/adc.h"

// RP2040: channel N maps to GPIO (26 + N) for channels 0..3.
// Channel 4 is the internal temp sensor — not exposed by this HAL.
static inline uint hal_adc_channel_to_gpio(hal_adc_channel_t ch) {
    return 26u + (uint)ch;
}

void hal_adc_init(void) {
    adc_init();
}

void hal_adc_channel_enable(hal_adc_channel_t ch) {
    adc_gpio_init(hal_adc_channel_to_gpio(ch));
}

uint16_t hal_adc_read(hal_adc_channel_t ch) {
    adc_select_input((uint)ch);
    return adc_read();
}

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

void hal_adc_fifo_setup(const hal_adc_fifo_cfg_t *cfg) {
    if (!cfg) return;
    adc_fifo_setup(cfg->enable_fifo, cfg->enable_dma,
                   (uint16_t)cfg->dreq_threshold,
                   false, cfg->shift_to_8bit);
}

void hal_adc_set_clkdiv(uint32_t div) {
    adc_set_clkdiv((float)div);
}

void hal_adc_run(bool enabled) {
    adc_run(enabled);
}

const volatile void *hal_adc_fifo_register(void) {
    return (const volatile void *)&adc_hw->fifo;
}

void hal_adc_select_input(uint8_t channel) {
    adc_select_input(channel);
}

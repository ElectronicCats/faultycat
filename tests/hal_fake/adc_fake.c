#include "hal_fake_adc.h"

#include <string.h>

bool                    hal_fake_adc_initialized;
uint32_t                hal_fake_adc_init_calls;
hal_fake_adc_channel_t  hal_fake_adc_channels[HAL_FAKE_ADC_MAX_CHANNELS];

void hal_fake_adc_reset(void) {
    hal_fake_adc_initialized = false;
    hal_fake_adc_init_calls  = 0;
    memset(hal_fake_adc_channels, 0, sizeof(hal_fake_adc_channels));
    memset(&hal_fake_adc_extra, 0, sizeof(hal_fake_adc_extra));
}

void hal_fake_adc_set_value(hal_adc_channel_t ch, uint16_t value) {
    if (ch >= HAL_FAKE_ADC_MAX_CHANNELS) {
        return;
    }
    hal_fake_adc_channels[ch].simulated_value = value;
}

void hal_adc_init(void) {
    hal_fake_adc_initialized = true;
    hal_fake_adc_init_calls++;
}

void hal_adc_channel_enable(hal_adc_channel_t ch) {
    if (ch >= HAL_FAKE_ADC_MAX_CHANNELS) {
        return;
    }
    hal_fake_adc_channels[ch].enabled = true;
    hal_fake_adc_channels[ch].enable_calls++;
}

uint16_t hal_adc_read(hal_adc_channel_t ch) {
    if (ch >= HAL_FAKE_ADC_MAX_CHANNELS) {
        return 0;
    }
    hal_fake_adc_channels[ch].read_calls++;
    return hal_fake_adc_channels[ch].simulated_value;
}

hal_fake_adc_extra_t hal_fake_adc_extra;

void hal_adc_fifo_setup(const hal_adc_fifo_cfg_t *cfg) {
    if (!cfg) return;
    hal_fake_adc_extra.fifo_setup_called = true;
    hal_fake_adc_extra.last_fifo_cfg     = *cfg;
}

void hal_adc_set_clkdiv(uint32_t div) {
    hal_fake_adc_extra.clkdiv = div;
}

void hal_adc_run(bool enabled) {
    hal_fake_adc_extra.running = enabled;
    hal_fake_adc_extra.run_calls++;
}

const volatile void *hal_adc_fifo_register(void) {
    static volatile uint8_t fake_fifo;
    return (const volatile void *)&fake_fifo;
}

void hal_adc_select_input(uint8_t channel) {
    hal_fake_adc_extra.selected_channel = channel;
    hal_fake_adc_extra.select_calls++;
}

#include "hal_fake_adc.h"

#include <string.h>

bool                    hal_fake_adc_initialized;
uint32_t                hal_fake_adc_init_calls;
hal_fake_adc_channel_t  hal_fake_adc_channels[HAL_FAKE_ADC_MAX_CHANNELS];

void hal_fake_adc_reset(void) {
    hal_fake_adc_initialized = false;
    hal_fake_adc_init_calls  = 0;
    memset(hal_fake_adc_channels, 0, sizeof(hal_fake_adc_channels));
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

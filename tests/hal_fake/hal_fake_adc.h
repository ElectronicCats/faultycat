#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/adc.h"

// Fake ADC tracks per-channel state and lets the test program the
// "simulated" sample that hal_adc_read() returns.

#define HAL_FAKE_ADC_MAX_CHANNELS 8

typedef struct {
    bool     enabled;
    uint16_t simulated_value;
    uint32_t read_calls;
    uint32_t enable_calls;
} hal_fake_adc_channel_t;

extern bool                     hal_fake_adc_initialized;
extern uint32_t                 hal_fake_adc_init_calls;
extern hal_fake_adc_channel_t   hal_fake_adc_channels[HAL_FAKE_ADC_MAX_CHANNELS];

void hal_fake_adc_reset(void);

// Program the value that the next (and subsequent) hal_adc_read(ch)
// will return until changed.
void hal_fake_adc_set_value(hal_adc_channel_t ch, uint16_t value);

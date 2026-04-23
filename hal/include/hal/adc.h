#pragma once

#include <stdint.h>

// HAL — ADC
//
// Portable access to the SAR ADC. RP2040 has channels 0..3 mapped to
// GP26..GP29 (and channel 4 = internal temperature sensor, not
// exposed here).
//
// Thin surface; streaming via DMA arrives in F4 when the glitch
// engine starts capturing waveforms during a glitch.

typedef uint8_t hal_adc_channel_t;

// Power up and reset the ADC peripheral. Safe to call more than once.
void hal_adc_init(void);

// Route the GPIO tied to `ch` to the ADC (disables the digital input
// buffer, no pulls). Must be called once per channel before reading
// that channel.
void hal_adc_channel_enable(hal_adc_channel_t ch);

// One-shot read: select `ch` and return its 12-bit sample (0..4095).
uint16_t hal_adc_read(hal_adc_channel_t ch);

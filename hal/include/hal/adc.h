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

// -----------------------------------------------------------------------------
// F4-4 — FIFO + DREQ surface for the EMFI capture ring. These map 1:1
// to pico-sdk adc_fifo_setup/adc_set_clkdiv/adc_run.
// -----------------------------------------------------------------------------

#include <stdbool.h>

typedef struct {
    bool     enable_dma;       // route samples to DMA via DREQ_ADC
    bool     shift_to_8bit;    // true = pushes 8-bit samples (matches legacy glitcher.c)
    uint32_t dreq_threshold;   // DREQ asserted when >= threshold samples present
    bool     enable_fifo;
} hal_adc_fifo_cfg_t;

// Configure the ADC FIFO + DREQ source. Must be called after
// hal_adc_select_input (via hal_adc_read_raw once or explicitly if
// a finer init lands later).
void hal_adc_fifo_setup(const hal_adc_fifo_cfg_t *cfg);

// 0 = full-speed ADC (~500 ksps). Higher values divide the sample rate.
void hal_adc_set_clkdiv(uint32_t div);

// Start / stop continuous conversion. While running, samples stream
// into the FIFO (and from there, via DMA, to the ring buffer).
void hal_adc_run(bool enabled);

// Pointer to the ADC FIFO register for DMA transfers. Opaque to
// callers; pass into hal_dma_configure as `src`.
const volatile void *hal_adc_fifo_register(void);

// Select the ADC input channel. Channel 3 corresponds to GP29 on
// RP2040 (target_monitor). Exposed here because emfi_capture selects
// it directly rather than going through target_monitor.
void hal_adc_select_input(uint8_t channel);

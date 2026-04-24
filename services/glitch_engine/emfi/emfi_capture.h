#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/glitch_engine/emfi/emfi_capture — 8 KB ADC DMA ring on
// GP29 (ADC ch 3), 8-bit samples at full ADC speed.
//
// Ported in spirit (and in PCB-specific constants) from the legacy
// firmware/c/glitcher/glitcher.c::prepare_adc — FaultyCat-origin code,
// BSD-3 clean. Upgraded to use hal/adc + hal/dma rather than pico-sdk
// directly.
//
// Lifecycle:
//   emfi_capture_init   → claim DMA channel, configure, leave idle
//   emfi_capture_start  → adc_run(true), arm the DMA ring
//   emfi_capture_stop   → adc_run(false), abort DMA, freeze buffer
//   emfi_capture_buffer → stable pointer into the 8 KB ring
//   emfi_capture_fill   → 0..8192, saturates after first full wrap

#define EMFI_CAPTURE_BUFFER_BYTES 8192u

bool emfi_capture_init(void);
void emfi_capture_start(void);
void emfi_capture_stop(void);
const uint8_t *emfi_capture_buffer(void);
uint32_t emfi_capture_fill(void);

// Test-only: drop cached state so emfi_capture_init re-runs from scratch.
// Unit tests call this in setUp alongside hal_fake_*_reset. On target the
// module's static state outlives calls, but the host-side tests reset the
// fakes each case, so the module must follow.
void emfi_capture_reset_for_test(void);

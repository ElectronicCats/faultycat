#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/dma.h"

#define HAL_FAKE_DMA_CHANNELS 12

typedef struct {
    bool           claimed;
    bool           busy;
    hal_dma_cfg_t  cfg;
    volatile void *dst;
    const volatile void *src;
    uint32_t       transfer_count;
    uint32_t       configure_calls;
    uint32_t       start_calls;
    uint32_t       abort_calls;
} hal_fake_dma_state_t;

extern hal_fake_dma_state_t hal_fake_dma_channels[HAL_FAKE_DMA_CHANNELS];

void hal_fake_dma_reset(void);

// Test-only hooks to let tests poke is_busy and transfer_count.
void hal_fake_dma_set_busy(hal_dma_channel_t ch, bool busy);
void hal_fake_dma_set_transfer_count(hal_dma_channel_t ch, uint32_t n);

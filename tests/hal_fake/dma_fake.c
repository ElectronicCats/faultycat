#include "hal_fake_dma.h"

#include <string.h>

hal_fake_dma_state_t hal_fake_dma_channels[HAL_FAKE_DMA_CHANNELS];

void hal_fake_dma_reset(void) {
    memset(hal_fake_dma_channels, 0, sizeof(hal_fake_dma_channels));
}

hal_dma_channel_t hal_dma_claim_unused(void) {
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (!hal_fake_dma_channels[i].claimed) {
            hal_fake_dma_channels[i].claimed = true;
            return i;
        }
    }
    return -1;
}

void hal_dma_unclaim(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].claimed = false;
    hal_fake_dma_channels[ch].busy    = false;
}

void hal_dma_configure(hal_dma_channel_t ch, const hal_dma_cfg_t *cfg,
                       volatile void *dst, const volatile void *src,
                       uint32_t transfer_count, bool start) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS || !cfg) return;
    hal_fake_dma_state_t *s = &hal_fake_dma_channels[ch];
    s->cfg            = *cfg;
    s->dst            = dst;
    s->src            = src;
    s->transfer_count = transfer_count;
    s->configure_calls++;
    if (start) {
        s->busy = true;
        s->start_calls++;
    }
}

void hal_dma_start(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].busy = true;
    hal_fake_dma_channels[ch].start_calls++;
}

void hal_dma_abort(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].busy = false;
    hal_fake_dma_channels[ch].abort_calls++;
}

bool hal_dma_is_busy(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return false;
    return hal_fake_dma_channels[ch].busy;
}

uint32_t hal_dma_transfer_count(hal_dma_channel_t ch) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return 0;
    return hal_fake_dma_channels[ch].transfer_count;
}

void hal_fake_dma_set_busy(hal_dma_channel_t ch, bool busy) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].busy = busy;
}

void hal_fake_dma_set_transfer_count(hal_dma_channel_t ch, uint32_t n) {
    if (ch < 0 || ch >= HAL_FAKE_DMA_CHANNELS) return;
    hal_fake_dma_channels[ch].transfer_count = n;
}

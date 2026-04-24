#include "hal/dma.h"

#include "hardware/dma.h"

static inline uint to_size(hal_dma_xfer_size_t s) {
    switch (s) {
        case HAL_DMA_SIZE_8:  return DMA_SIZE_8;
        case HAL_DMA_SIZE_16: return DMA_SIZE_16;
        case HAL_DMA_SIZE_32: return DMA_SIZE_32;
    }
    return DMA_SIZE_8;
}

hal_dma_channel_t hal_dma_claim_unused(void) {
    int ch = dma_claim_unused_channel(false);
    return ch;  // pico-sdk returns -1 on fail when required=false
}

void hal_dma_unclaim(hal_dma_channel_t ch) {
    if (ch < 0) return;
    dma_channel_unclaim((uint)ch);
}

void hal_dma_configure(hal_dma_channel_t ch, const hal_dma_cfg_t *cfg,
                       volatile void *dst, const volatile void *src,
                       uint32_t transfer_count, bool start) {
    if (ch < 0 || !cfg) return;
    dma_channel_config c = dma_channel_get_default_config((uint)ch);
    channel_config_set_transfer_data_size(&c, to_size(cfg->size));
    channel_config_set_read_increment(&c, cfg->read_increment);
    channel_config_set_write_increment(&c, cfg->write_increment);
    if (cfg->ring_bits) {
        channel_config_set_ring(&c, cfg->ring_on_write, (uint)cfg->ring_bits);
    }
    channel_config_set_dreq(&c, (uint)cfg->dreq);
    dma_channel_configure((uint)ch, &c, dst, src, transfer_count, start);
}

void hal_dma_start(hal_dma_channel_t ch) {
    if (ch < 0) return;
    dma_channel_start((uint)ch);
}

void hal_dma_abort(hal_dma_channel_t ch) {
    if (ch < 0) return;
    dma_channel_abort((uint)ch);
}

bool hal_dma_is_busy(hal_dma_channel_t ch) {
    if (ch < 0) return false;
    return dma_channel_is_busy((uint)ch);
}

uint32_t hal_dma_transfer_count(hal_dma_channel_t ch) {
    if (ch < 0) return 0;
    return dma_channel_hw_addr((uint)ch)->transfer_count;
}

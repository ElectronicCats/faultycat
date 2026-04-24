#include "emfi_capture.h"

#include <string.h>

#include "hal/adc.h"
#include "hal/dma.h"

#define EMFI_CAPTURE_ADC_CHANNEL  3u   // GP29 = target_monitor
#define EMFI_CAPTURE_RING_BITS    13u  // 2^13 = 8192
#define EMFI_CAPTURE_DREQ_THRESH  1u

static uint8_t           s_buffer[EMFI_CAPTURE_BUFFER_BYTES];
static hal_dma_channel_t s_dma   = -1;
static bool              s_init  = false;
static bool              s_running = false;

bool emfi_capture_init(void) {
    if (s_init) return true;
    s_dma = hal_dma_claim_unused();
    if (s_dma < 0) return false;

    // Idempotent — matches pico-sdk adc_init semantics. Ensures the ADC
    // peripheral is up even when no other driver (e.g. target_monitor)
    // has run yet — F4-5 orchestration doesn't depend on caller order.
    hal_adc_init();
    hal_adc_select_input(EMFI_CAPTURE_ADC_CHANNEL);
    hal_adc_fifo_setup(&(hal_adc_fifo_cfg_t){
        .enable_fifo      = true,
        .enable_dma       = true,
        .dreq_threshold   = EMFI_CAPTURE_DREQ_THRESH,
        .shift_to_8bit    = true,
    });
    hal_adc_set_clkdiv(0);  // full speed

    hal_dma_cfg_t cfg = {
        .size            = HAL_DMA_SIZE_8,
        .read_increment  = false,
        .write_increment = true,
        .ring_bits       = EMFI_CAPTURE_RING_BITS,
        .ring_on_write   = true,
        .dreq            = HAL_DMA_DREQ_ADC,
    };
    memset(s_buffer, 0, sizeof(s_buffer));
    hal_dma_configure(s_dma, &cfg,
                     s_buffer,
                     hal_adc_fifo_register(),
                     0xFFFFFFFFu,
                     false);
    s_init = true;
    s_running = false;
    return true;
}

void emfi_capture_start(void) {
    if (!s_init || s_running) return;
    // (re-)arm the transfer from offset 0 by restarting the DMA.
    // A fresh configure with start=true resets write_address to s_buffer.
    hal_dma_cfg_t cfg = {
        .size            = HAL_DMA_SIZE_8,
        .read_increment  = false,
        .write_increment = true,
        .ring_bits       = EMFI_CAPTURE_RING_BITS,
        .ring_on_write   = true,
        .dreq            = HAL_DMA_DREQ_ADC,
    };
    hal_dma_configure(s_dma, &cfg,
                     s_buffer,
                     hal_adc_fifo_register(),
                     0xFFFFFFFFu,
                     true);
    hal_adc_run(true);
    s_running = true;
}

void emfi_capture_stop(void) {
    if (!s_init || !s_running) return;
    hal_adc_run(false);
    hal_dma_abort(s_dma);
    s_running = false;
}

const uint8_t *emfi_capture_buffer(void) {
    return s_buffer;
}

uint32_t emfi_capture_fill(void) {
    if (!s_init) return 0;
    uint32_t remaining = hal_dma_transfer_count(s_dma);
    if (remaining == 0xFFFFFFFFu) return 0;
    // Every pushed sample decrements the transfer_count from the
    // starting 0xFFFFFFFF. Once it has decremented by ≥ 8192 the ring
    // has wrapped at least once.
    uint32_t pushed = 0xFFFFFFFFu - remaining;
    if (pushed >= EMFI_CAPTURE_BUFFER_BYTES) return EMFI_CAPTURE_BUFFER_BYTES;
    return pushed;
}

void emfi_capture_reset_for_test(void) {
    s_dma     = -1;
    s_init    = false;
    s_running = false;
}

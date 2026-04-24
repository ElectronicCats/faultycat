#include "emfi_capture.h"

#include <string.h>

#include "hal/adc.h"
#include "hal/dma.h"

#define EMFI_CAPTURE_ADC_CHANNEL  3u   // GP29 = target_monitor
#define EMFI_CAPTURE_RING_BITS    13u  // 2^13 = 8192
#define EMFI_CAPTURE_DREQ_THRESH  1u

// RP2040 DMA ring mode wraps the WRITE address using the lower N bits
// (N = ring_bits). For wraps to stay inside s_buffer, the buffer MUST
// be aligned to (1 << ring_bits) bytes = 8192 here. Without this,
// ring wraps overrun into adjacent memory — empirically observed to
// corrupt the TinyUSB stack and wedge the main loop after a few
// seconds of continuous capture.
static uint8_t           s_buffer[EMFI_CAPTURE_BUFFER_BYTES]
                         __attribute__((aligned(EMFI_CAPTURE_BUFFER_BYTES)));
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

    // Don't enable FIFO/DMA at init — target_monitor_read_raw uses
    // adc_read() (single-shot), which blocks forever if FIFO+DMA mode
    // is already enabled. Capture lifecycle takes ownership of the
    // ADC FIFO only between start() and stop().
    memset(s_buffer, 0, sizeof(s_buffer));
    s_init = true;
    s_running = false;
    return true;
}

void emfi_capture_start(void) {
    if (!s_init || s_running) return;

    // Take ownership of the ADC FIFO for the duration of the capture.
    hal_adc_select_input(EMFI_CAPTURE_ADC_CHANNEL);
    hal_adc_fifo_setup(&(hal_adc_fifo_cfg_t){
        .enable_fifo      = true,
        .enable_dma       = true,
        .dreq_threshold   = EMFI_CAPTURE_DREQ_THRESH,
        .shift_to_8bit    = true,
    });
    hal_adc_set_clkdiv(0);  // full speed

    // (re-)arm the transfer from offset 0. A fresh configure with
    // start=true resets write_address to s_buffer.
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

    // Release FIFO ownership so target_monitor_read_raw's single-shot
    // adc_read() works again.
    hal_adc_fifo_setup(&(hal_adc_fifo_cfg_t){
        .enable_fifo      = false,
        .enable_dma       = false,
        .dreq_threshold   = 0,
        .shift_to_8bit    = false,
    });
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

bool emfi_capture_is_running(void) {
    return s_running;
}

void emfi_capture_reset_for_test(void) {
    s_dma     = -1;
    s_init    = false;
    s_running = false;
}

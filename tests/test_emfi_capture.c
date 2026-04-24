// Unit tests for services/glitch_engine/emfi/emfi_capture.

#include "unity.h"

#include "emfi_capture.h"
#include "hal/dma.h"
#include "hal_fake_adc.h"
#include "hal_fake_dma.h"

void setUp(void) {
    hal_fake_adc_reset();
    hal_fake_dma_reset();
    emfi_capture_reset_for_test();
}
void tearDown(void) {
    emfi_capture_stop();
}

static void test_init_claims_one_dma_channel(void) {
    TEST_ASSERT_TRUE(emfi_capture_init());
    int used = 0;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) used++;
    }
    TEST_ASSERT_EQUAL_INT(1, used);
}

static void test_init_does_not_touch_adc_fifo(void) {
    // Init must NOT enable the ADC FIFO — target_monitor_read_raw's
    // single-shot adc_read() would block forever if it did.
    emfi_capture_init();
    TEST_ASSERT_FALSE(hal_fake_adc_extra.fifo_setup_called);
    TEST_ASSERT_FALSE(hal_fake_adc_extra.running);
}

static void test_start_selects_adc_channel_3(void) {
    emfi_capture_init();
    emfi_capture_start();
    TEST_ASSERT_EQUAL_UINT8(3, hal_fake_adc_extra.selected_channel);
}

static void test_start_configures_fifo_for_8bit_dma(void) {
    emfi_capture_init();
    emfi_capture_start();
    TEST_ASSERT_TRUE(hal_fake_adc_extra.fifo_setup_called);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.last_fifo_cfg.enable_dma);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.last_fifo_cfg.shift_to_8bit);
    TEST_ASSERT_EQUAL_UINT32(1u,
        hal_fake_adc_extra.last_fifo_cfg.dreq_threshold);
}

static void test_start_sets_full_speed_clkdiv(void) {
    emfi_capture_init();
    emfi_capture_start();
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_adc_extra.clkdiv);
}

static void test_stop_releases_adc_fifo(void) {
    // After stop, FIFO must be disabled so target_monitor_read_raw
    // works again.
    emfi_capture_init();
    emfi_capture_start();
    emfi_capture_stop();
    TEST_ASSERT_FALSE(hal_fake_adc_extra.last_fifo_cfg.enable_fifo);
    TEST_ASSERT_FALSE(hal_fake_adc_extra.last_fifo_cfg.enable_dma);
}

static void test_start_arms_dma_and_runs_adc(void) {
    emfi_capture_init();
    emfi_capture_start();
    // One claimed channel is now busy.
    int busy_count = 0;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].busy) busy_count++;
    }
    TEST_ASSERT_EQUAL_INT(1, busy_count);
    TEST_ASSERT_TRUE(hal_fake_adc_extra.running);
}

static void test_start_configures_ring_mode_8192_bytes(void) {
    emfi_capture_init();
    emfi_capture_start();
    int ch = -1;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) { ch = i; break; }
    }
    TEST_ASSERT_NOT_EQUAL(-1, ch);
    TEST_ASSERT_EQUAL_UINT32(13u, hal_fake_dma_channels[ch].cfg.ring_bits);
    TEST_ASSERT_TRUE(hal_fake_dma_channels[ch].cfg.ring_on_write);
    TEST_ASSERT_EQUAL_UINT(HAL_DMA_DREQ_ADC, hal_fake_dma_channels[ch].cfg.dreq);
}

static void test_stop_halts_adc_and_aborts_dma(void) {
    emfi_capture_init();
    emfi_capture_start();
    emfi_capture_stop();
    TEST_ASSERT_FALSE(hal_fake_adc_extra.running);
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) {
            TEST_ASSERT_FALSE(hal_fake_dma_channels[i].busy);
        }
    }
}

static void test_buffer_pointer_is_stable(void) {
    emfi_capture_init();
    const uint8_t *a = emfi_capture_buffer();
    emfi_capture_start();
    const uint8_t *b = emfi_capture_buffer();
    emfi_capture_stop();
    const uint8_t *c = emfi_capture_buffer();
    TEST_ASSERT_EQUAL_PTR(a, b);
    TEST_ASSERT_EQUAL_PTR(b, c);
}

static void test_fill_saturates_at_8192(void) {
    emfi_capture_init();
    emfi_capture_start();
    int ch = -1;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) { ch = i; break; }
    }
    // Simulate 1000 samples pushed.
    hal_fake_dma_set_transfer_count(ch, 0xFFFFFFFFu - 1000u);
    TEST_ASSERT_EQUAL_UINT32(1000u, emfi_capture_fill());
    // Simulate full wrap + then some.
    hal_fake_dma_set_transfer_count(ch, 0xFFFFFFFFu - 20000u);
    TEST_ASSERT_EQUAL_UINT32(8192u, emfi_capture_fill());
}

static void test_init_idempotent(void) {
    TEST_ASSERT_TRUE(emfi_capture_init());
    TEST_ASSERT_TRUE(emfi_capture_init());  // no second claim
    int used = 0;
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        if (hal_fake_dma_channels[i].claimed) used++;
    }
    TEST_ASSERT_EQUAL_INT(1, used);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_claims_one_dma_channel);
    RUN_TEST(test_init_does_not_touch_adc_fifo);
    RUN_TEST(test_start_selects_adc_channel_3);
    RUN_TEST(test_start_configures_fifo_for_8bit_dma);
    RUN_TEST(test_start_sets_full_speed_clkdiv);
    RUN_TEST(test_start_arms_dma_and_runs_adc);
    RUN_TEST(test_start_configures_ring_mode_8192_bytes);
    RUN_TEST(test_stop_halts_adc_and_aborts_dma);
    RUN_TEST(test_stop_releases_adc_fifo);
    RUN_TEST(test_buffer_pointer_is_stable);
    RUN_TEST(test_fill_saturates_at_8192);
    RUN_TEST(test_init_idempotent);
    return UNITY_END();
}

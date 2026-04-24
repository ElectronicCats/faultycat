// Unit tests for hal/dma — exercised against the host fake.

#include "unity.h"

#include "hal/dma.h"
#include "hal_fake_dma.h"

void setUp(void) { hal_fake_dma_reset(); }
void tearDown(void) {}

static void test_claim_unused_returns_sequential_channels(void) {
    TEST_ASSERT_EQUAL_INT(0, hal_dma_claim_unused());
    TEST_ASSERT_EQUAL_INT(1, hal_dma_claim_unused());
    TEST_ASSERT_EQUAL_INT(2, hal_dma_claim_unused());
}

static void test_claim_unused_exhausts_pool(void) {
    for (int i = 0; i < HAL_FAKE_DMA_CHANNELS; i++) {
        TEST_ASSERT_EQUAL_INT(i, hal_dma_claim_unused());
    }
    TEST_ASSERT_EQUAL_INT(-1, hal_dma_claim_unused());
}

static void test_unclaim_returns_channel_to_pool(void) {
    int a = hal_dma_claim_unused();
    int b = hal_dma_claim_unused();
    hal_dma_unclaim(a);
    // next claim reuses the freed slot
    TEST_ASSERT_EQUAL_INT(a, hal_dma_claim_unused());
    (void)b;
}

static void test_configure_records_fields(void) {
    int ch = hal_dma_claim_unused();
    hal_dma_cfg_t cfg = {
        .size = HAL_DMA_SIZE_8,
        .read_increment  = false,
        .write_increment = true,
        .ring_bits       = 13,          // 8192 bytes
        .ring_on_write   = true,
        .dreq            = HAL_DMA_DREQ_ADC,
    };
    static uint8_t buf[8192];
    hal_dma_configure(ch, &cfg, buf, (void *)0xDEADBEEF, 0xFFFFFFFFu, true);
    TEST_ASSERT_EQUAL_UINT(HAL_DMA_SIZE_8, hal_fake_dma_channels[ch].cfg.size);
    TEST_ASSERT_EQUAL_UINT32(13u, hal_fake_dma_channels[ch].cfg.ring_bits);
    TEST_ASSERT_TRUE(hal_fake_dma_channels[ch].cfg.ring_on_write);
    TEST_ASSERT_EQUAL_UINT(HAL_DMA_DREQ_ADC, hal_fake_dma_channels[ch].cfg.dreq);
    TEST_ASSERT_EQUAL_PTR(buf, (void *)hal_fake_dma_channels[ch].dst);
    TEST_ASSERT_TRUE(hal_dma_is_busy(ch));
}

static void test_configure_without_start_does_not_arm(void) {
    int ch = hal_dma_claim_unused();
    hal_dma_cfg_t cfg = { .size = HAL_DMA_SIZE_8, .dreq = HAL_DMA_DREQ_ADC };
    hal_dma_configure(ch, &cfg, (void *)1, (void *)2, 8u, false);
    TEST_ASSERT_FALSE(hal_dma_is_busy(ch));
    hal_dma_start(ch);
    TEST_ASSERT_TRUE(hal_dma_is_busy(ch));
}

static void test_abort_stops_transfer(void) {
    int ch = hal_dma_claim_unused();
    hal_dma_cfg_t cfg = { .size = HAL_DMA_SIZE_8 };
    hal_dma_configure(ch, &cfg, (void *)1, (void *)2, 8u, true);
    TEST_ASSERT_TRUE(hal_dma_is_busy(ch));
    hal_dma_abort(ch);
    TEST_ASSERT_FALSE(hal_dma_is_busy(ch));
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_dma_channels[ch].abort_calls);
}

static void test_transfer_count_reports_set_value(void) {
    int ch = hal_dma_claim_unused();
    hal_fake_dma_set_transfer_count(ch, 1234u);
    TEST_ASSERT_EQUAL_UINT32(1234u, hal_dma_transfer_count(ch));
}

static void test_out_of_range_is_a_noop(void) {
    TEST_ASSERT_FALSE(hal_dma_is_busy(-1));
    TEST_ASSERT_FALSE(hal_dma_is_busy(HAL_FAKE_DMA_CHANNELS));
    hal_dma_unclaim(-1);   // must not crash
    hal_dma_abort(-1);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_claim_unused_returns_sequential_channels);
    RUN_TEST(test_claim_unused_exhausts_pool);
    RUN_TEST(test_unclaim_returns_channel_to_pool);
    RUN_TEST(test_configure_records_fields);
    RUN_TEST(test_configure_without_start_does_not_arm);
    RUN_TEST(test_abort_stops_transfer);
    RUN_TEST(test_transfer_count_reports_set_value);
    RUN_TEST(test_out_of_range_is_a_noop);
    return UNITY_END();
}

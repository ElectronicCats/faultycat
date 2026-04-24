// Unit tests for hal/pio — exercised against the host fake.

#include "unity.h"

#include "hal/pio.h"
#include "hal_fake_pio.h"

void setUp(void) { hal_fake_pio_reset(); }
void tearDown(void) {}

static void test_instance_returns_distinct_handles(void) {
    hal_pio_inst_t *a = hal_pio_instance(0);
    hal_pio_inst_t *b = hal_pio_instance(1);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_NOT_EQUAL(a, b);
    TEST_ASSERT_NULL(hal_pio_instance(2));
}

static void test_add_program_returns_offset_and_marks_loaded(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    uint16_t prog[] = { 0xE000, 0xE001, 0xE002 };
    hal_pio_program_t p = { .instructions = prog, .length = 3, .origin = -1 };
    TEST_ASSERT_TRUE(hal_pio_can_add_program(pio, &p));
    uint32_t offset = 0xFFFFFFFF;
    TEST_ASSERT_TRUE(hal_pio_add_program(pio, &p, &offset));
    TEST_ASSERT_EQUAL_UINT32(0u, offset);
    TEST_ASSERT_FALSE(hal_pio_can_add_program(pio, &p));  // slot full
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].program.loaded);
    TEST_ASSERT_EQUAL_UINT32(3u, hal_fake_pio_insts[0].program.length);
}

static void test_remove_program_frees_slot(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    uint16_t prog[] = { 0xE000 };
    hal_pio_program_t p = { .instructions = prog, .length = 1, .origin = -1 };
    uint32_t off = 0;
    hal_pio_add_program(pio, &p, &off);
    hal_pio_remove_program(pio, &p, off);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].program.loaded);
    TEST_ASSERT_TRUE(hal_pio_can_add_program(pio, &p));
}

static void test_clear_instruction_memory_counts_and_frees(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_clear_instruction_memory(pio);
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[0].clear_memory_calls);
}

static void test_claim_sm_is_exclusive(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    TEST_ASSERT_TRUE(hal_pio_claim_sm(pio, 0));
    TEST_ASSERT_FALSE(hal_pio_claim_sm(pio, 0));
    hal_pio_unclaim_sm(pio, 0);
    TEST_ASSERT_TRUE(hal_pio_claim_sm(pio, 0));
    // Out-of-range sm rejected.
    TEST_ASSERT_FALSE(hal_pio_claim_sm(pio, 4));
}

static void test_configure_records_offset_and_cfg(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_cfg_t cfg = {
        .set_pin_base = 14, .set_pin_count = 1,
        .sideset_pin_base = 0, .sideset_pin_count = 0,
        .in_pin_base = 8, .clk_div = 1.0f,
    };
    hal_pio_sm_configure(pio, 2, 5u, &cfg);
    TEST_ASSERT_EQUAL_UINT32(5u, hal_fake_pio_insts[0].sm[2].configured_offset);
    TEST_ASSERT_EQUAL_UINT32(14u, hal_fake_pio_insts[0].sm[2].last_cfg.set_pin_base);
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[0].sm[2].configure_calls);
}

static void test_sm_enable_tracks_state(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_set_enabled(pio, 1, true);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[1].enabled);
    hal_pio_sm_set_enabled(pio, 1, false);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[1].enabled);
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[1].enable_calls);
}

static void test_fifo_put_and_get_round_trip(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_put_blocking(pio, 0, 0xDEADBEEF);
    hal_pio_sm_put_blocking(pio, 0, 0xCAFEBABE);
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[0].tx_count);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEF, hal_fake_pio_insts[0].sm[0].tx_fifo[0]);
    TEST_ASSERT_EQUAL_HEX32(0xCAFEBABE, hal_fake_pio_insts[0].sm[0].tx_fifo[1]);

    // push_rx simulates PIO → CPU and try_get drains in FIFO order.
    hal_fake_pio_push_rx(0, 0, 0x11);
    hal_fake_pio_push_rx(0, 0, 0x22);
    uint32_t got = 0;
    TEST_ASSERT_TRUE(hal_pio_sm_try_get(pio, 0, &got));
    TEST_ASSERT_EQUAL_HEX32(0x11, got);
    TEST_ASSERT_TRUE(hal_pio_sm_try_get(pio, 0, &got));
    TEST_ASSERT_EQUAL_HEX32(0x22, got);
    TEST_ASSERT_FALSE(hal_pio_sm_try_get(pio, 0, &got));
}

static void test_clear_fifos_drops_both_sides(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_put_blocking(pio, 3, 0xAA);
    hal_fake_pio_push_rx(0, 3, 0xBB);
    hal_pio_sm_clear_fifos(pio, 3);
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_pio_insts[0].sm[3].tx_count);
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_pio_insts[0].sm[3].rx_count);
}

static void test_irq_raise_get_clear(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    TEST_ASSERT_FALSE(hal_pio_irq_get(pio, 0));
    hal_fake_pio_raise_irq(0, 0);
    TEST_ASSERT_TRUE(hal_pio_irq_get(pio, 0));
    hal_pio_irq_clear(pio, 0);
    TEST_ASSERT_FALSE(hal_pio_irq_get(pio, 0));
}

static void test_gpio_init_sets_bitmap(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_gpio_init(pio, 14);
    hal_pio_gpio_init(pio, 8);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].gpio_init_bitmap & (1u << 14));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].gpio_init_bitmap & (1u << 8));
}

static void test_sm_restart_counts_calls(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_sm_restart(pio, 2);
    hal_pio_sm_restart(pio, 2);
    TEST_ASSERT_EQUAL_UINT32(2u, hal_fake_pio_insts[0].sm[2].restart_calls);
}

static void test_set_consecutive_pindirs_records_args(void) {
    hal_pio_inst_t *pio = hal_pio_instance(0);
    hal_pio_set_consecutive_pindirs(pio, 0, 14, 1, true);
    TEST_ASSERT_EQUAL_UINT32(1u, hal_fake_pio_insts[0].sm[0].pindirs_calls);
    TEST_ASSERT_EQUAL_UINT32(14u, hal_fake_pio_insts[0].sm[0].last_pindirs_base);
    TEST_ASSERT_EQUAL_UINT32(1u,  hal_fake_pio_insts[0].sm[0].last_pindirs_count);
    TEST_ASSERT_TRUE(hal_fake_pio_insts[0].sm[0].last_pindirs_is_out);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_instance_returns_distinct_handles);
    RUN_TEST(test_add_program_returns_offset_and_marks_loaded);
    RUN_TEST(test_remove_program_frees_slot);
    RUN_TEST(test_clear_instruction_memory_counts_and_frees);
    RUN_TEST(test_claim_sm_is_exclusive);
    RUN_TEST(test_configure_records_offset_and_cfg);
    RUN_TEST(test_sm_enable_tracks_state);
    RUN_TEST(test_fifo_put_and_get_round_trip);
    RUN_TEST(test_clear_fifos_drops_both_sides);
    RUN_TEST(test_irq_raise_get_clear);
    RUN_TEST(test_gpio_init_sets_bitmap);
    RUN_TEST(test_sm_restart_counts_calls);
    RUN_TEST(test_set_consecutive_pindirs_records_args);
    return UNITY_END();
}

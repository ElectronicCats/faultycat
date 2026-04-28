// Unit tests for services/campaign_manager — exercises the F9-2
// state machine + sweep generator + ringbuffer + step executor
// plumbing, all without engine integration (F9-3 adds that).

#include "unity.h"

#include <string.h>

#include "campaign_manager.h"

void setUp(void) {
    campaign_manager_init();
}

void tearDown(void) {}

// -----------------------------------------------------------------------------
// Pure helpers — axis math
// -----------------------------------------------------------------------------

static void test_axis_step_count_collapse_when_step_zero(void) {
    campaign_axis_t a = { .start = 100, .end = 500, .step = 0 };
    TEST_ASSERT_EQUAL_UINT32(1u, campaign_axis_step_count(&a));
}

static void test_axis_step_count_inclusive(void) {
    campaign_axis_t a = { .start = 100, .end = 105, .step = 1 };
    TEST_ASSERT_EQUAL_UINT32(6u, campaign_axis_step_count(&a));   // 100,101,102,103,104,105
}

static void test_axis_step_count_with_step(void) {
    campaign_axis_t a = { .start = 100, .end = 200, .step = 25 };
    TEST_ASSERT_EQUAL_UINT32(5u, campaign_axis_step_count(&a));   // 100,125,150,175,200
}

static void test_axis_step_count_partial_step_truncates(void) {
    campaign_axis_t a = { .start = 100, .end = 199, .step = 25 };
    TEST_ASSERT_EQUAL_UINT32(4u, campaign_axis_step_count(&a));   // 100,125,150,175 (199 not reached)
}

static void test_axis_step_count_inverted_returns_zero(void) {
    campaign_axis_t a = { .start = 500, .end = 100, .step = 1 };
    TEST_ASSERT_EQUAL_UINT32(0u, campaign_axis_step_count(&a));
}

static void test_axis_step_count_null_safe(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, campaign_axis_step_count(NULL));
}

// -----------------------------------------------------------------------------
// Pure helpers — total + step_to_params
// -----------------------------------------------------------------------------

static void test_total_steps_cartesian(void) {
    campaign_config_t cfg = {
        .engine = CAMPAIGN_ENGINE_EMFI,
        .delay  = { 100, 105, 1 },     // 6 values
        .width  = { 1, 5, 2 },         // 1,3,5 → 3 values
        .power  = { 50, 50, 0 },       // collapsed → 1 value
        .settle_ms = 0,
    };
    TEST_ASSERT_EQUAL_UINT32(18u, campaign_total_steps(&cfg));   // 6×3×1
}

static void test_total_steps_zero_when_axis_inverted(void) {
    campaign_config_t cfg = {
        .engine = CAMPAIGN_ENGINE_EMFI,
        .delay  = { 500, 100, 1 },     // inverted → 0
        .width  = { 1, 5, 1 },
        .power  = { 50, 50, 0 },
    };
    TEST_ASSERT_EQUAL_UINT32(0u, campaign_total_steps(&cfg));
}

static void test_step_to_params_lex_order(void) {
    // 2×2×2 = 8 steps. Iteration order: power innermost, width
    // middle, delay outermost.
    campaign_config_t cfg = {
        .engine = CAMPAIGN_ENGINE_EMFI,
        .delay  = { 10, 20, 10 },      // 10, 20
        .width  = { 1, 2, 1 },         // 1, 2
        .power  = { 100, 200, 100 },   // 100, 200
    };

    static const struct {
        uint32_t expect_d, expect_w, expect_p;
    } expected[] = {
        {10, 1, 100}, {10, 1, 200}, {10, 2, 100}, {10, 2, 200},
        {20, 1, 100}, {20, 1, 200}, {20, 2, 100}, {20, 2, 200},
    };

    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t d = 0, w = 0, p = 0;
        TEST_ASSERT_TRUE(campaign_step_to_params(&cfg, i, &d, &w, &p));
        TEST_ASSERT_EQUAL_UINT32(expected[i].expect_d, d);
        TEST_ASSERT_EQUAL_UINT32(expected[i].expect_w, w);
        TEST_ASSERT_EQUAL_UINT32(expected[i].expect_p, p);
    }
}

static void test_step_to_params_out_of_range(void) {
    campaign_config_t cfg = {
        .engine = CAMPAIGN_ENGINE_EMFI,
        .delay  = { 1, 1, 0 }, .width = { 1, 1, 0 }, .power = { 1, 1, 0 },
    };
    uint32_t d, w, p;
    TEST_ASSERT_FALSE(campaign_step_to_params(&cfg, 1u, &d, &w, &p));   // total=1, step=1 OOR
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

static campaign_config_t small_config(void) {
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_EMFI,
        .delay     = { 10, 10, 0 },    // 1 step
        .width     = { 1, 3, 1 },      // 3 steps
        .power     = { 100, 100, 0 },  // 1 step
        .settle_ms = 0,
    };
    return cfg;
}

static void test_init_starts_idle(void) {
    campaign_status_t st;
    campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_IDLE, st.state);
    TEST_ASSERT_EQUAL(CAMPAIGN_ERR_NONE, st.err);
}

static void test_configure_transitions_to_configuring(void) {
    campaign_config_t cfg = small_config();
    TEST_ASSERT_TRUE(campaign_manager_configure(&cfg));
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_CONFIGURING, st.state);
    TEST_ASSERT_EQUAL_UINT32(3u, st.total_steps);
}

static void test_configure_invalid_returns_false(void) {
    campaign_config_t cfg = {
        .engine = CAMPAIGN_ENGINE_EMFI,
        .delay  = { 100, 50, 1 },     // inverted → total = 0
        .width  = { 1, 1, 0 },
        .power  = { 1, 1, 0 },
    };
    TEST_ASSERT_FALSE(campaign_manager_configure(&cfg));
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_ERR_BAD_CONFIG, st.err);
}

static void test_start_without_configure_returns_false(void) {
    TEST_ASSERT_FALSE(campaign_manager_start());
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_ERR_NOT_CONFIGURED, st.err);
}

static void test_start_after_configure_transitions_to_sweeping(void) {
    campaign_config_t cfg = small_config();
    TEST_ASSERT_TRUE(campaign_manager_configure(&cfg));
    TEST_ASSERT_TRUE(campaign_manager_start());
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_SWEEPING, st.state);
}

static void test_tick_advances_through_steps_to_done(void) {
    campaign_config_t cfg = small_config();   // 3 steps
    campaign_manager_configure(&cfg);
    campaign_manager_start();

    for (uint32_t i = 0; i < 3u; i++) {
        campaign_manager_tick();
    }
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_DONE, st.state);
    TEST_ASSERT_EQUAL_UINT32(3u, st.results_pushed);
    TEST_ASSERT_EQUAL_UINT32(3u, st.step_n);
}

static void test_stop_transitions_to_stopped(void) {
    campaign_config_t cfg = small_config();
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    campaign_manager_tick();   // 1 step done
    campaign_manager_stop();

    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_STOPPED, st.state);
    // Further ticks are no-ops in STOPPED.
    campaign_manager_tick();
    campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL_UINT32(1u, st.results_pushed);
}

static void test_tick_in_idle_is_noop(void) {
    campaign_manager_tick();
    campaign_manager_tick();
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_IDLE, st.state);
    TEST_ASSERT_EQUAL_UINT32(0u, st.results_pushed);
}

// -----------------------------------------------------------------------------
// Result drain + ringbuffer
// -----------------------------------------------------------------------------

static void test_drain_yields_pushed_results(void) {
    campaign_config_t cfg = small_config();   // 3 steps
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    for (int i = 0; i < 3; i++) campaign_manager_tick();

    campaign_result_t out[8];
    size_t n = campaign_manager_drain_results(out, 8);
    TEST_ASSERT_EQUAL_size_t(3u, n);
    // Results match the lex order: width axis 1,2,3; delay/power collapsed.
    TEST_ASSERT_EQUAL_UINT32(0u, out[0].step_n);
    TEST_ASSERT_EQUAL_UINT32(1u, out[0].width);
    TEST_ASSERT_EQUAL_UINT32(1u, out[1].step_n);
    TEST_ASSERT_EQUAL_UINT32(2u, out[1].width);
    TEST_ASSERT_EQUAL_UINT32(2u, out[2].step_n);
    TEST_ASSERT_EQUAL_UINT32(3u, out[2].width);

    // Ring should be empty now.
    TEST_ASSERT_EQUAL_size_t(0u, campaign_manager_drain_results(out, 8));
}

static void test_drain_partial(void) {
    campaign_config_t cfg = small_config();   // 3 steps
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    for (int i = 0; i < 3; i++) campaign_manager_tick();

    campaign_result_t out[2];
    TEST_ASSERT_EQUAL_size_t(2u, campaign_manager_drain_results(out, 2));
    TEST_ASSERT_EQUAL_size_t(1u, campaign_manager_drain_results(out, 2));
    TEST_ASSERT_EQUAL_size_t(0u, campaign_manager_drain_results(out, 2));
}

static void test_ringbuffer_overflow_drops_results(void) {
    // Configure a sweep larger than CAMPAIGN_RESULT_RING_DEPTH (256).
    // Width 1..300 step 1 → 300 steps.
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_EMFI,
        .delay     = { 0, 0, 0 },
        .width     = { 1, 300, 1 },
        .power     = { 0, 0, 0 },
        .settle_ms = 0,
    };
    TEST_ASSERT_TRUE(campaign_manager_configure(&cfg));
    TEST_ASSERT_TRUE(campaign_manager_start());

    // Run all 300 ticks WITHOUT draining. Ring fills to 256, then
    // the remaining 44 are dropped.
    for (uint32_t i = 0; i < 300u; i++) campaign_manager_tick();

    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_DONE, st.state);
    TEST_ASSERT_EQUAL_UINT32(256u, st.results_pushed);
    TEST_ASSERT_EQUAL_UINT32(44u,  st.results_dropped);
}

// -----------------------------------------------------------------------------
// Custom step executor
// -----------------------------------------------------------------------------

typedef struct {
    uint32_t calls;
    uint32_t last_step;
    uint32_t last_delay;
    uint32_t last_width;
    uint32_t last_power;
    bool     return_value;
    uint8_t  fire_status_to_set;
    uint8_t  verify_status_to_set;
    uint32_t target_state_to_set;
} test_executor_state_t;

static bool capturing_executor(uint32_t step, const campaign_config_t *cfg,
                               uint32_t delay, uint32_t width, uint32_t power,
                               uint8_t *fire, uint8_t *verify, uint32_t *target,
                               void *user) {
    (void)cfg;
    test_executor_state_t *s = (test_executor_state_t *)user;
    s->calls++;
    s->last_step = step;
    s->last_delay = delay;
    s->last_width = width;
    s->last_power = power;
    if (fire)   *fire   = s->fire_status_to_set;
    if (verify) *verify = s->verify_status_to_set;
    if (target) *target = s->target_state_to_set;
    return s->return_value;
}

static void test_custom_executor_called_once_per_step(void) {
    test_executor_state_t s = { .return_value = true };
    campaign_manager_set_step_executor(capturing_executor, &s);

    campaign_config_t cfg = small_config();   // 3 steps
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    for (int i = 0; i < 3; i++) campaign_manager_tick();

    TEST_ASSERT_EQUAL_UINT32(3u, s.calls);
    TEST_ASSERT_EQUAL_UINT32(2u, s.last_step);    // last call was step 2
    TEST_ASSERT_EQUAL_UINT32(3u, s.last_width);   // axis end
}

static void test_custom_executor_status_propagates_to_result(void) {
    test_executor_state_t s = {
        .return_value         = true,
        .fire_status_to_set   = 0x42,
        .verify_status_to_set = 0xAB,
        .target_state_to_set  = 0xDEADBEEFu,
    };
    campaign_manager_set_step_executor(capturing_executor, &s);

    campaign_config_t cfg = small_config();
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    campaign_manager_tick();

    campaign_result_t out;
    TEST_ASSERT_EQUAL_size_t(1u, campaign_manager_drain_results(&out, 1));
    TEST_ASSERT_EQUAL_HEX8(0x42, out.fire_status);
    TEST_ASSERT_EQUAL_HEX8(0xAB, out.verify_status);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, out.target_state);
}

static void test_executor_failure_transitions_to_error(void) {
    test_executor_state_t s = {
        .return_value       = false,            // executor reports failure
        .fire_status_to_set = 0x99,             // engine-specific code
    };
    campaign_manager_set_step_executor(capturing_executor, &s);

    campaign_config_t cfg = small_config();   // 3 steps
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    campaign_manager_tick();   // step 0 fails

    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_ERROR, st.state);
    TEST_ASSERT_EQUAL(CAMPAIGN_ERR_STEP_FAILED, st.err);
    // Result still pushed for diagnostics.
    TEST_ASSERT_EQUAL_UINT32(1u, st.results_pushed);

    campaign_result_t out;
    campaign_manager_drain_results(&out, 1);
    TEST_ASSERT_EQUAL_HEX8(0x99, out.fire_status);
}

static void test_default_executor_is_noop(void) {
    campaign_config_t cfg = small_config();
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    campaign_manager_tick();

    campaign_result_t out;
    campaign_manager_drain_results(&out, 1);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.fire_status);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.verify_status);
    TEST_ASSERT_EQUAL_HEX32(0u,   out.target_state);
}

// -----------------------------------------------------------------------------
// Reconfigure mid-sweep is rejected
// -----------------------------------------------------------------------------

static void test_reconfigure_mid_sweep_rejected(void) {
    campaign_config_t cfg = small_config();
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    campaign_manager_tick();

    campaign_config_t cfg2 = cfg;
    cfg2.width.end = 5;
    TEST_ASSERT_FALSE(campaign_manager_configure(&cfg2));
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_SWEEPING, st.state);   // unchanged
}

// -----------------------------------------------------------------------------
// Result record size — fixed by F9 D1 spec (28 B; the original
// proposal said 24 B but the per-field math gives 28 once the
// reserved[2] alignment slot for future ack flags is included).
// -----------------------------------------------------------------------------

static void test_result_record_is_28_bytes(void) {
    TEST_ASSERT_EQUAL_size_t(28u, sizeof(campaign_result_t));
}

// -----------------------------------------------------------------------------
// Runner
// -----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_axis_step_count_collapse_when_step_zero);
    RUN_TEST(test_axis_step_count_inclusive);
    RUN_TEST(test_axis_step_count_with_step);
    RUN_TEST(test_axis_step_count_partial_step_truncates);
    RUN_TEST(test_axis_step_count_inverted_returns_zero);
    RUN_TEST(test_axis_step_count_null_safe);

    RUN_TEST(test_total_steps_cartesian);
    RUN_TEST(test_total_steps_zero_when_axis_inverted);
    RUN_TEST(test_step_to_params_lex_order);
    RUN_TEST(test_step_to_params_out_of_range);

    RUN_TEST(test_init_starts_idle);
    RUN_TEST(test_configure_transitions_to_configuring);
    RUN_TEST(test_configure_invalid_returns_false);
    RUN_TEST(test_start_without_configure_returns_false);
    RUN_TEST(test_start_after_configure_transitions_to_sweeping);
    RUN_TEST(test_tick_advances_through_steps_to_done);
    RUN_TEST(test_stop_transitions_to_stopped);
    RUN_TEST(test_tick_in_idle_is_noop);

    RUN_TEST(test_drain_yields_pushed_results);
    RUN_TEST(test_drain_partial);
    RUN_TEST(test_ringbuffer_overflow_drops_results);

    RUN_TEST(test_custom_executor_called_once_per_step);
    RUN_TEST(test_custom_executor_status_propagates_to_result);
    RUN_TEST(test_executor_failure_transitions_to_error);
    RUN_TEST(test_default_executor_is_noop);

    RUN_TEST(test_reconfigure_mid_sweep_rejected);

    RUN_TEST(test_result_record_is_28_bytes);

    return UNITY_END();
}

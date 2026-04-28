// Unit tests for services/host_proto/campaign_proto — config decode,
// status encode, drain encode. Tests drive campaign_manager directly
// to populate the ringbuffer / state, then assert the wire layout.

#include "unity.h"

#include <string.h>

#include "campaign_manager.h"
#include "campaign_proto.h"

void setUp(void) {
    campaign_manager_init();
}

void tearDown(void) {}

// -----------------------------------------------------------------------------
// Helpers — pack a 40-byte CONFIG payload from u32 LE fields.
// -----------------------------------------------------------------------------

static void pack_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v        & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16)& 0xFFu);
    p[3] = (uint8_t)((v >> 24)& 0xFFu);
}

static uint32_t unpack_le32(const uint8_t *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static void build_config_payload(uint8_t *out,
                                 uint32_t ds, uint32_t de, uint32_t dp,
                                 uint32_t ws, uint32_t we, uint32_t wp,
                                 uint32_t ps, uint32_t pe, uint32_t pp,
                                 uint32_t settle_ms) {
    pack_le32(&out[0],  ds); pack_le32(&out[4],  de); pack_le32(&out[8],  dp);
    pack_le32(&out[12], ws); pack_le32(&out[16], we); pack_le32(&out[20], wp);
    pack_le32(&out[24], ps); pack_le32(&out[28], pe); pack_le32(&out[32], pp);
    pack_le32(&out[36], settle_ms);
}

// -----------------------------------------------------------------------------
// decode_config
// -----------------------------------------------------------------------------

static void test_decode_config_valid(void) {
    uint8_t payload[CAMPAIGN_CONFIG_PAYLOAD_LEN];
    build_config_payload(payload,
                         100, 200, 50,    // delay
                         1, 5, 1,         // width
                         10, 10, 0,       // power (collapsed)
                         50);             // settle

    campaign_config_t cfg;
    TEST_ASSERT_TRUE(campaign_proto_decode_config(payload, sizeof(payload),
                                                  CAMPAIGN_ENGINE_EMFI, &cfg));
    TEST_ASSERT_EQUAL(CAMPAIGN_ENGINE_EMFI, cfg.engine);
    TEST_ASSERT_EQUAL_UINT32(100u, cfg.delay.start);
    TEST_ASSERT_EQUAL_UINT32(200u, cfg.delay.end);
    TEST_ASSERT_EQUAL_UINT32(50u,  cfg.delay.step);
    TEST_ASSERT_EQUAL_UINT32(1u,   cfg.width.start);
    TEST_ASSERT_EQUAL_UINT32(5u,   cfg.width.end);
    TEST_ASSERT_EQUAL_UINT32(1u,   cfg.width.step);
    TEST_ASSERT_EQUAL_UINT32(10u,  cfg.power.start);
    TEST_ASSERT_EQUAL_UINT32(10u,  cfg.power.end);
    TEST_ASSERT_EQUAL_UINT32(0u,   cfg.power.step);
    TEST_ASSERT_EQUAL_UINT32(50u,  cfg.settle_ms);
}

static void test_decode_config_engine_set_by_caller(void) {
    uint8_t payload[CAMPAIGN_CONFIG_PAYLOAD_LEN] = {0};
    pack_le32(&payload[0], 1); pack_le32(&payload[4], 1);   // delay
    pack_le32(&payload[12], 1); pack_le32(&payload[16], 1); // width
    pack_le32(&payload[24], 1); pack_le32(&payload[28], 1); // power

    campaign_config_t cfg;
    campaign_proto_decode_config(payload, sizeof(payload),
                                 CAMPAIGN_ENGINE_CROWBAR, &cfg);
    TEST_ASSERT_EQUAL(CAMPAIGN_ENGINE_CROWBAR, cfg.engine);
}

static void test_decode_config_wrong_length_rejected(void) {
    uint8_t payload[CAMPAIGN_CONFIG_PAYLOAD_LEN - 1] = {0};
    campaign_config_t cfg;
    TEST_ASSERT_FALSE(campaign_proto_decode_config(payload, sizeof(payload),
                                                   CAMPAIGN_ENGINE_EMFI, &cfg));
}

static void test_decode_config_null_safe(void) {
    uint8_t payload[CAMPAIGN_CONFIG_PAYLOAD_LEN] = {0};
    campaign_config_t cfg;
    TEST_ASSERT_FALSE(campaign_proto_decode_config(NULL, sizeof(payload),
                                                   CAMPAIGN_ENGINE_EMFI, &cfg));
    TEST_ASSERT_FALSE(campaign_proto_decode_config(payload, sizeof(payload),
                                                   CAMPAIGN_ENGINE_EMFI, NULL));
}

// -----------------------------------------------------------------------------
// apply_config
// -----------------------------------------------------------------------------

static void test_apply_config_valid_returns_ok(void) {
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_EMFI,
        .delay     = { 100, 100, 0 },
        .width     = { 1, 3, 1 },
        .power     = { 0, 0, 0 },
        .settle_ms = 0,
    };
    TEST_ASSERT_EQUAL_HEX8(CAMPAIGN_PROTO_OK, campaign_proto_apply_config(&cfg));
}

static void test_apply_config_invalid_returns_rejected(void) {
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_EMFI,
        .delay     = { 200, 100, 1 },   // inverted → 0 steps
        .width     = { 1, 1, 0 },
        .power     = { 0, 0, 0 },
    };
    TEST_ASSERT_EQUAL_HEX8(CAMPAIGN_PROTO_ERR_REJECTED,
                           campaign_proto_apply_config(&cfg));
}

static void test_apply_config_null_returns_bad_len(void) {
    TEST_ASSERT_EQUAL_HEX8(CAMPAIGN_PROTO_ERR_BAD_LEN,
                           campaign_proto_apply_config(NULL));
}

// -----------------------------------------------------------------------------
// serialize_status
// -----------------------------------------------------------------------------

static void test_serialize_status_layout(void) {
    // Default state — IDLE / 0 / 0 / 0.
    uint8_t out[CAMPAIGN_STATUS_REPLY_LEN] = {0xCC};
    size_t n = campaign_proto_serialize_status(out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(CAMPAIGN_STATUS_REPLY_LEN, n);
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_IDLE, out[0]);
    TEST_ASSERT_EQUAL(CAMPAIGN_ERR_NONE,   out[1]);
    TEST_ASSERT_EQUAL_HEX8(0u, out[2]);
    TEST_ASSERT_EQUAL_HEX8(0u, out[3]);
    TEST_ASSERT_EQUAL_UINT32(0u, unpack_le32(&out[4]));
    TEST_ASSERT_EQUAL_UINT32(0u, unpack_le32(&out[8]));
    TEST_ASSERT_EQUAL_UINT32(0u, unpack_le32(&out[12]));
    TEST_ASSERT_EQUAL_UINT32(0u, unpack_le32(&out[16]));
}

static void test_serialize_status_after_running_campaign(void) {
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_EMFI,
        .delay     = { 10, 10, 0 },
        .width     = { 1, 3, 1 },        // 3 steps
        .power     = { 0, 0, 0 },
        .settle_ms = 0,
    };
    TEST_ASSERT_TRUE(campaign_manager_configure(&cfg));
    TEST_ASSERT_TRUE(campaign_manager_start());
    for (int i = 0; i < 3; i++) campaign_manager_tick();

    uint8_t out[CAMPAIGN_STATUS_REPLY_LEN];
    campaign_proto_serialize_status(out, sizeof(out));
    TEST_ASSERT_EQUAL(CAMPAIGN_STATE_DONE, out[0]);
    TEST_ASSERT_EQUAL_UINT32(3u, unpack_le32(&out[4]));    // step_n
    TEST_ASSERT_EQUAL_UINT32(3u, unpack_le32(&out[8]));    // total_steps
    TEST_ASSERT_EQUAL_UINT32(3u, unpack_le32(&out[12]));   // results_pushed
    TEST_ASSERT_EQUAL_UINT32(0u, unpack_le32(&out[16]));   // results_dropped
}

static void test_serialize_status_too_small_buffer(void) {
    uint8_t out[CAMPAIGN_STATUS_REPLY_LEN - 1];
    TEST_ASSERT_EQUAL_size_t(0u, campaign_proto_serialize_status(out, sizeof(out)));
}

// -----------------------------------------------------------------------------
// serialize_drain
// -----------------------------------------------------------------------------

static void run_3_step_campaign(void) {
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_CROWBAR,
        .delay     = { 100, 100, 0 },
        .width     = { 10, 30, 10 },     // 3 values: 10,20,30
        .power     = { 1, 1, 0 },
        .settle_ms = 0,
    };
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    for (int i = 0; i < 3; i++) campaign_manager_tick();
}

static void test_serialize_drain_empty(void) {
    // No campaign run → ring is empty → header n=0 only.
    uint8_t out[64];
    size_t n = campaign_proto_serialize_drain(out, sizeof(out), 8u);
    TEST_ASSERT_EQUAL_size_t(1u, n);
    TEST_ASSERT_EQUAL_HEX8(0u, out[0]);
}

static void test_serialize_drain_3_results(void) {
    run_3_step_campaign();
    uint8_t out[1u + 3u * 28u];
    size_t n = campaign_proto_serialize_drain(out, sizeof(out), 8u);
    TEST_ASSERT_EQUAL_size_t(1u + 3u * 28u, n);
    TEST_ASSERT_EQUAL_HEX8(3u, out[0]);
    // First record: step=0, delay=100, width=10, power=1.
    TEST_ASSERT_EQUAL_UINT32(0u,   unpack_le32(&out[1 + 0]));
    TEST_ASSERT_EQUAL_UINT32(100u, unpack_le32(&out[1 + 4]));
    TEST_ASSERT_EQUAL_UINT32(10u,  unpack_le32(&out[1 + 8]));
    TEST_ASSERT_EQUAL_UINT32(1u,   unpack_le32(&out[1 + 12]));
    // Second record at offset 1 + 28 = 29.
    TEST_ASSERT_EQUAL_UINT32(1u,   unpack_le32(&out[29 + 0]));
    TEST_ASSERT_EQUAL_UINT32(20u,  unpack_le32(&out[29 + 8]));
}

static void test_serialize_drain_caps_at_max_count(void) {
    // Run a 5-step campaign, request 2 → only 2 in the reply.
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_EMFI,
        .delay     = { 1, 5, 1 },        // 5 values
        .width     = { 1, 1, 0 },
        .power     = { 0, 0, 0 },
        .settle_ms = 0,
    };
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    for (int i = 0; i < 5; i++) campaign_manager_tick();

    uint8_t out[1u + 2u * 28u];
    size_t n = campaign_proto_serialize_drain(out, sizeof(out), 2u);
    TEST_ASSERT_EQUAL_size_t(1u + 2u * 28u, n);
    TEST_ASSERT_EQUAL_HEX8(2u, out[0]);
    // Remaining 3 results stay in the ring for the next drain.
    campaign_status_t st; campaign_manager_get_status(&st);
    TEST_ASSERT_EQUAL_UINT32(5u, st.results_pushed);
}

static void test_serialize_drain_caps_at_buffer_space(void) {
    // Buffer fits only 1 record; max_count=8 should be capped to 1.
    run_3_step_campaign();
    uint8_t out[1u + 1u * 28u];
    size_t n = campaign_proto_serialize_drain(out, sizeof(out), 8u);
    TEST_ASSERT_EQUAL_size_t(1u + 1u * 28u, n);
    TEST_ASSERT_EQUAL_HEX8(1u, out[0]);
}

static void test_serialize_drain_caps_at_proto_max(void) {
    // Even when buffer is huge, max_count is clamped to
    // CAMPAIGN_DRAIN_MAX_COUNT (18).
    // Run 25 steps, request 100; reply contains 18 records.
    campaign_config_t cfg = {
        .engine    = CAMPAIGN_ENGINE_EMFI,
        .delay     = { 1, 25, 1 },
        .width     = { 1, 1, 0 },
        .power     = { 0, 0, 0 },
        .settle_ms = 0,
    };
    campaign_manager_configure(&cfg);
    campaign_manager_start();
    for (int i = 0; i < 25; i++) campaign_manager_tick();

    uint8_t out[1u + 32u * 28u];
    size_t n = campaign_proto_serialize_drain(out, sizeof(out), 100u);
    TEST_ASSERT_EQUAL_size_t(1u + 18u * 28u, n);
    TEST_ASSERT_EQUAL_HEX8(18u, out[0]);
}

static void test_serialize_drain_too_small_buffer(void) {
    uint8_t out[0];
    TEST_ASSERT_EQUAL_size_t(0u, campaign_proto_serialize_drain(out, 0, 8u));
}

// -----------------------------------------------------------------------------
// Constants self-check
// -----------------------------------------------------------------------------

static void test_payload_layout_constants(void) {
    TEST_ASSERT_EQUAL_size_t(40u, CAMPAIGN_CONFIG_PAYLOAD_LEN);
    TEST_ASSERT_EQUAL_size_t(20u, CAMPAIGN_STATUS_REPLY_LEN);
    TEST_ASSERT_EQUAL_HEX8(0x20u, CAMPAIGN_CMD_CONFIG);
    TEST_ASSERT_EQUAL_HEX8(0x21u, CAMPAIGN_CMD_START);
    TEST_ASSERT_EQUAL_HEX8(0x22u, CAMPAIGN_CMD_STOP);
    TEST_ASSERT_EQUAL_HEX8(0x23u, CAMPAIGN_CMD_STATUS);
    TEST_ASSERT_EQUAL_HEX8(0x24u, CAMPAIGN_CMD_DRAIN);
}

// -----------------------------------------------------------------------------
// Runner
// -----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_decode_config_valid);
    RUN_TEST(test_decode_config_engine_set_by_caller);
    RUN_TEST(test_decode_config_wrong_length_rejected);
    RUN_TEST(test_decode_config_null_safe);

    RUN_TEST(test_apply_config_valid_returns_ok);
    RUN_TEST(test_apply_config_invalid_returns_rejected);
    RUN_TEST(test_apply_config_null_returns_bad_len);

    RUN_TEST(test_serialize_status_layout);
    RUN_TEST(test_serialize_status_after_running_campaign);
    RUN_TEST(test_serialize_status_too_small_buffer);

    RUN_TEST(test_serialize_drain_empty);
    RUN_TEST(test_serialize_drain_3_results);
    RUN_TEST(test_serialize_drain_caps_at_max_count);
    RUN_TEST(test_serialize_drain_caps_at_buffer_space);
    RUN_TEST(test_serialize_drain_caps_at_proto_max);
    RUN_TEST(test_serialize_drain_too_small_buffer);

    RUN_TEST(test_payload_layout_constants);

    return UNITY_END();
}

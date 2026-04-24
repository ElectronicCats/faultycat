// Unit tests for services/host_proto/crowbar_proto.

#include "unity.h"

#include <string.h>

#include "board_v2.h"
#include "crowbar_campaign.h"
#include "crowbar_mosfet.h"
#include "crowbar_pio.h"
#include "crowbar_proto.h"
#include "ext_trigger.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "hal_fake_time.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
    hal_fake_pio_reset();
    crowbar_mosfet_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    crowbar_campaign_init();
    crowbar_proto_init();
}
void tearDown(void) {
    crowbar_campaign_disarm();
}

static bool feed_frame(const uint8_t *bytes, size_t n) {
    bool ready = false;
    for (size_t i = 0; i < n; i++) {
        ready = crowbar_proto_feed(bytes[i], 0u);
    }
    return ready;
}

static void append_crc(uint8_t *frame, uint16_t body_len) {
    uint16_t crc = crowbar_proto_crc16(&frame[1], 3u + body_len);
    frame[4 + body_len]     = (uint8_t)crc;
    frame[4 + body_len + 1] = (uint8_t)(crc >> 8);
}

// -----------------------------------------------------------------------------

static void test_crc_known_value(void) {
    // CRC-16/CCITT(False) of "123456789" = 0x29B1.
    TEST_ASSERT_EQUAL_HEX16(0x29B1u,
        crowbar_proto_crc16((const uint8_t *)"123456789", 9));
}

static void test_ping_assembles_and_replies_with_F5(void) {
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(10u, n);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_PROTO_SOF, reply[0]);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_CMD_PING | 0x80u, reply[1]);
    TEST_ASSERT_EQUAL_UINT8('F', reply[4]);
    TEST_ASSERT_EQUAL_UINT8('5', reply[5]);
}

static void test_bad_sof_is_ignored(void) {
    TEST_ASSERT_FALSE(crowbar_proto_feed(0x00u, 0u));
    TEST_ASSERT_FALSE(crowbar_proto_feed(0xFFu, 0u));
}

static void test_bad_crc_is_rejected(void) {
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_PING, 0, 0, 0xAA, 0xBB };
    TEST_ASSERT_FALSE(feed_frame(frame, sizeof(frame)));
}

static void test_len_overflow_resets_parser(void) {
    // CROWBAR_PROTO_MAX_PAYLOAD = 64, so len=0x0100 (256) overflows.
    uint8_t hdr[4] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_PING, 0x00, 0x01 };
    for (size_t i = 0; i < sizeof(hdr); i++) crowbar_proto_feed(hdr[i], 0u);
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
}

static void test_inter_byte_timeout_resets_parser(void) {
    crowbar_proto_feed(CROWBAR_PROTO_SOF, 0u);
    crowbar_proto_feed(CROWBAR_CMD_PING, 50u);
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    bool ok = false;
    ok = crowbar_proto_feed(frame[0], 250u);    // > 100 ms gap
    TEST_ASSERT_FALSE(ok);
    for (size_t i = 1; i < sizeof(frame); i++) {
        ok = crowbar_proto_feed(frame[i], 250u + i);
    }
    TEST_ASSERT_TRUE(ok);
}

static void test_configure_cmd_validates_and_acks(void) {
    // payload: trigger(1) + output(1) + delay_us(4) + width_ns(4) = 10
    uint8_t frame[4 + 10 + 2];
    frame[0] = CROWBAR_PROTO_SOF;
    frame[1] = CROWBAR_CMD_CONFIGURE;
    frame[2] = 10; frame[3] = 0;
    frame[4] = CROWBAR_TRIG_IMMEDIATE;
    frame[5] = CROWBAR_OUT_HP;
    // delay_us = 100
    frame[6] = 100; frame[7] = 0; frame[8] = 0; frame[9] = 0;
    // width_ns = 200
    frame[10] = 200; frame[11] = 0; frame[12] = 0; frame[13] = 0;
    append_crc(frame, 10);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_CMD_CONFIGURE | 0x80u, reply[1]);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_ERR_NONE, reply[4]);
}

static void test_configure_rejects_output_none(void) {
    uint8_t frame[4 + 10 + 2];
    frame[0] = CROWBAR_PROTO_SOF;
    frame[1] = CROWBAR_CMD_CONFIGURE;
    frame[2] = 10; frame[3] = 0;
    frame[4] = CROWBAR_TRIG_IMMEDIATE;
    frame[5] = CROWBAR_OUT_NONE;
    frame[6] = 100; frame[7] = 0; frame[8] = 0; frame[9] = 0;
    frame[10] = 200; frame[11] = 0; frame[12] = 0; frame[13] = 0;
    append_crc(frame, 10);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_ERR_BAD_CONFIG, reply[4]);
}

static void test_configure_rejects_short_payload(void) {
    uint8_t frame[4 + 5 + 2];
    frame[0] = CROWBAR_PROTO_SOF;
    frame[1] = CROWBAR_CMD_CONFIGURE;
    frame[2] = 5; frame[3] = 0;
    memset(&frame[4], 0, 5);
    append_crc(frame, 5);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_ERR_BAD_CONFIG, reply[4]);
}

static void test_status_payload_is_15_bytes(void) {
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_STATUS, 0, 0, 0, 0 };
    append_crc(frame, 0);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(21u, n);   // 4 hdr + 15 payload + 2 CRC
    TEST_ASSERT_EQUAL_UINT8(15u, reply[2]);
    TEST_ASSERT_EQUAL_UINT8(0u,  reply[3]);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_STATE_IDLE, reply[4]);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_OUT_NONE,   reply[18]);
}

static void test_arm_without_configure_returns_bad_config(void) {
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_ARM, 0, 0, 0, 0 };
    append_crc(frame, 0);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_ERR_BAD_CONFIG, reply[4]);
}

static void test_disarm_replies_no_error(void) {
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, CROWBAR_CMD_DISARM, 0, 0, 0, 0 };
    append_crc(frame, 0);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_ERR_NONE, reply[4]);
}

static void test_unknown_cmd_returns_bad_config(void) {
    uint8_t frame[6] = { CROWBAR_PROTO_SOF, 0x77, 0, 0, 0, 0 };
    append_crc(frame, 0);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(0x77 | 0x80u, reply[1]);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_ERR_BAD_CONFIG, reply[4]);
}

static void test_fire_short_payload_returns_bad_config(void) {
    uint8_t frame[4 + 2 + 2];
    frame[0] = CROWBAR_PROTO_SOF;
    frame[1] = CROWBAR_CMD_FIRE;
    frame[2] = 2; frame[3] = 0;
    frame[4] = 0; frame[5] = 0;
    append_crc(frame, 2);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = crowbar_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(CROWBAR_ERR_BAD_CONFIG, reply[4]);
}

static void test_fuzz_random_bytes_never_crash(void) {
    uint32_t x = 0xC0FFEEu;
    for (int i = 0; i < 10000; i++) {
        x = x * 1664525u + 1013904223u;
        uint8_t b = (uint8_t)(x >> 17);
        if (crowbar_proto_feed(b, (uint32_t)i)) {
            uint8_t reply[256];
            (void)crowbar_proto_dispatch(reply, sizeof(reply));
        }
    }
    TEST_ASSERT_TRUE(true);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_known_value);
    RUN_TEST(test_ping_assembles_and_replies_with_F5);
    RUN_TEST(test_bad_sof_is_ignored);
    RUN_TEST(test_bad_crc_is_rejected);
    RUN_TEST(test_len_overflow_resets_parser);
    RUN_TEST(test_inter_byte_timeout_resets_parser);
    RUN_TEST(test_configure_cmd_validates_and_acks);
    RUN_TEST(test_configure_rejects_output_none);
    RUN_TEST(test_configure_rejects_short_payload);
    RUN_TEST(test_status_payload_is_15_bytes);
    RUN_TEST(test_arm_without_configure_returns_bad_config);
    RUN_TEST(test_disarm_replies_no_error);
    RUN_TEST(test_unknown_cmd_returns_bad_config);
    RUN_TEST(test_fire_short_payload_returns_bad_config);
    RUN_TEST(test_fuzz_random_bytes_never_crash);
    return UNITY_END();
}

// Unit tests for services/host_proto/emfi_proto.

#include "unity.h"

#include <string.h>

#include "board_v2.h"
#include "emfi_campaign.h"
#include "emfi_proto.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal_fake_adc.h"
#include "hal_fake_dma.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "hal_fake_pwm.h"
#include "hal_fake_time.h"
#include "hv_charger.h"

void setUp(void) {
    hal_fake_gpio_reset();
    hal_fake_time_reset();
    hal_fake_adc_reset();
    hal_fake_dma_reset();
    hal_fake_pio_reset();
    hal_fake_pwm_reset();
    hv_charger_init();
    emfi_pulse_init();
    ext_trigger_init(EXT_TRIGGER_PULL_DOWN);
    emfi_campaign_init();
    emfi_proto_init();
}
void tearDown(void) {
    emfi_campaign_disarm();
}

static bool feed_frame(const uint8_t *bytes, size_t n) {
    bool ready = false;
    for (size_t i = 0; i < n; i++) {
        ready = emfi_proto_feed(bytes[i], 0u);
    }
    return ready;
}

static void append_crc(uint8_t *frame, uint16_t body_len) {
    uint16_t crc = emfi_proto_crc16(&frame[1], 3u + body_len);
    frame[4 + body_len]     = (uint8_t)crc;
    frame[4 + body_len + 1] = (uint8_t)(crc >> 8);
}

static void test_crc_known_value(void) {
    // CRC-16/CCITT of "123456789" = 0x29B1.
    TEST_ASSERT_EQUAL_HEX16(0x29B1u,
        emfi_proto_crc16((const uint8_t *)"123456789", 9));
}

static void test_ping_assembles_and_replies(void) {
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(10u, n);  // SOF + CMD + 2 LEN + 4 payload + 2 CRC
    TEST_ASSERT_EQUAL_UINT8(EMFI_PROTO_SOF, reply[0]);
    TEST_ASSERT_EQUAL_UINT8(EMFI_CMD_PING | 0x80u, reply[1]);
    TEST_ASSERT_EQUAL_UINT8('F', reply[4]);
    TEST_ASSERT_EQUAL_UINT8('4', reply[5]);
}

static void test_bad_sof_is_ignored(void) {
    TEST_ASSERT_FALSE(emfi_proto_feed(0x00u, 0u));
    TEST_ASSERT_FALSE(emfi_proto_feed(0xFFu, 0u));
}

static void test_bad_crc_is_rejected(void) {
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0xAA, 0xBB };
    TEST_ASSERT_FALSE(feed_frame(frame, sizeof(frame)));
}

static void test_len_overflow_resets_parser(void) {
    uint8_t hdr[4] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0x01, 0x03 };  // len=769
    for (size_t i = 0; i < sizeof(hdr); i++) emfi_proto_feed(hdr[i], 0u);
    // Parser must be back at SOF; a new SOF should start a fresh frame.
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
}

static void test_inter_byte_timeout_resets_parser(void) {
    emfi_proto_feed(EMFI_PROTO_SOF, 0u);
    emfi_proto_feed(EMFI_CMD_PING, 50u);
    // Gap of 200 ms > 100 ms inter-byte timeout.
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_PING, 0, 0, 0, 0 };
    append_crc(frame, 0);
    bool ok = false;
    ok = emfi_proto_feed(frame[0], 250u);
    TEST_ASSERT_FALSE(ok);
    for (size_t i = 1; i < sizeof(frame); i++) {
        ok = emfi_proto_feed(frame[i], 250u + i);
    }
    TEST_ASSERT_TRUE(ok);
}

static void test_configure_cmd_validates_and_acks(void) {
    // payload: trigger(1) + delay_us(4) + width_us(4) + charge_timeout_ms(4)
    uint8_t frame[4 + 13 + 2];
    frame[0] = EMFI_PROTO_SOF;
    frame[1] = EMFI_CMD_CONFIGURE;
    frame[2] = 13;
    frame[3] = 0;
    frame[4] = EMFI_TRIG_IMMEDIATE;
    // delay_us = 100
    frame[5]  = 100; frame[6]  = 0; frame[7]  = 0; frame[8]  = 0;
    // width_us = 5
    frame[9]  = 5;   frame[10] = 0; frame[11] = 0; frame[12] = 0;
    // charge_timeout_ms = 1000
    frame[13] = 0xE8; frame[14] = 0x03; frame[15] = 0; frame[16] = 0;
    append_crc(frame, 13);
    TEST_ASSERT_TRUE(feed_frame(frame, sizeof(frame)));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);  // SOF+CMD+2LEN+1err+2CRC
    TEST_ASSERT_EQUAL_UINT8(EMFI_CMD_CONFIGURE | 0x80u, reply[1]);
    TEST_ASSERT_EQUAL_UINT8(EMFI_ERR_NONE, reply[4]);
}

static void test_configure_rejects_width_51(void) {
    uint8_t frame[4 + 13 + 2];
    frame[0] = EMFI_PROTO_SOF;
    frame[1] = EMFI_CMD_CONFIGURE;
    frame[2] = 13; frame[3] = 0;
    frame[4] = EMFI_TRIG_IMMEDIATE;
    frame[5]  = 100; frame[6]  = 0; frame[7]  = 0; frame[8]  = 0;
    frame[9]  = 51;  frame[10] = 0; frame[11] = 0; frame[12] = 0;
    frame[13] = 0;   frame[14] = 0; frame[15] = 0; frame[16] = 0;
    append_crc(frame, 13);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(EMFI_ERR_BAD_CONFIG, reply[4]);
}

static void test_status_payload_is_18_bytes(void) {
    uint8_t frame[6] = { EMFI_PROTO_SOF, EMFI_CMD_STATUS, 0, 0, 0, 0 };
    append_crc(frame, 0);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(24u, n);  // 4 hdr + 18 payload + 2 CRC
    TEST_ASSERT_EQUAL_UINT8(18u, reply[2]);
    TEST_ASSERT_EQUAL_UINT8(0u,  reply[3]);
    TEST_ASSERT_EQUAL_UINT8(EMFI_STATE_IDLE, reply[4]);
}

static void test_capture_rejects_out_of_range(void) {
    uint8_t frame[4 + 4 + 2];
    frame[0] = EMFI_PROTO_SOF;
    frame[1] = EMFI_CMD_CAPTURE;
    frame[2] = 4; frame[3] = 0;
    // offset 8000 + len 512 = 8512 > 8192
    frame[4] = 0x40; frame[5] = 0x1F;
    frame[6] = 0x00; frame[7] = 0x02;
    append_crc(frame, 4);
    feed_frame(frame, sizeof(frame));
    uint8_t reply[32] = { 0 };
    size_t n = emfi_proto_dispatch(reply, sizeof(reply));
    TEST_ASSERT_EQUAL_UINT(7u, n);
    TEST_ASSERT_EQUAL_UINT8(EMFI_ERR_BAD_CONFIG, reply[4]);
}

static void test_fuzz_random_bytes_never_crash(void) {
    // Seed-deterministic pseudo-random stream. We only need that
    // feed_frame never reads beyond its buffer + that dispatch after
    // each "true" return is safe. This is a smoke test, not a proof.
    uint32_t x = 0xC0FFEEu;
    for (int i = 0; i < 10000; i++) {
        x = x * 1664525u + 1013904223u;
        uint8_t b = (uint8_t)(x >> 17);
        if (emfi_proto_feed(b, (uint32_t)i)) {
            uint8_t reply[768];
            (void)emfi_proto_dispatch(reply, sizeof(reply));
        }
    }
    TEST_ASSERT_TRUE(true);  // reached here = no crash
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_known_value);
    RUN_TEST(test_ping_assembles_and_replies);
    RUN_TEST(test_bad_sof_is_ignored);
    RUN_TEST(test_bad_crc_is_rejected);
    RUN_TEST(test_len_overflow_resets_parser);
    RUN_TEST(test_inter_byte_timeout_resets_parser);
    RUN_TEST(test_configure_cmd_validates_and_acks);
    RUN_TEST(test_configure_rejects_width_51);
    RUN_TEST(test_status_payload_is_18_bytes);
    RUN_TEST(test_capture_rejects_out_of_range);
    RUN_TEST(test_fuzz_random_bytes_never_crash);
    return UNITY_END();
}

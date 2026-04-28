// Unit tests for services/buspirate_compat — feed bytes one at a
// time, capture write_byte / jtag_clock_bit / on_exit calls into
// fixtures, assert the BusPirate binary protocol shape.

#include "unity.h"

#include <string.h>

#include "buspirate_compat.h"

// -----------------------------------------------------------------------------
// Test fixtures
// -----------------------------------------------------------------------------

#define MAX_WRITES   1024u
#define MAX_CLOCKS   8192u

static uint8_t s_writes[MAX_WRITES];
static size_t  s_writes_len;

typedef struct { bool tms; bool tdi; } clock_event_t;
static clock_event_t s_clocks[MAX_CLOCKS];
static size_t        s_clocks_len;

static bool   s_tdo_script[MAX_CLOCKS];
static size_t s_tdo_script_len;
static size_t s_tdo_script_cursor;

static int    s_exit_count;

static void fix_write(uint8_t b, void *u) {
    (void)u;
    if (s_writes_len < MAX_WRITES) s_writes[s_writes_len++] = b;
}

static bool fix_clock(bool tms, bool tdi, void *u) {
    (void)u;
    if (s_clocks_len < MAX_CLOCKS) {
        s_clocks[s_clocks_len++] = (clock_event_t){.tms = tms, .tdi = tdi};
    }
    if (s_tdo_script_cursor < s_tdo_script_len) {
        return s_tdo_script[s_tdo_script_cursor++];
    }
    return false;
}

static void fix_exit(void *u) {
    (void)u;
    s_exit_count++;
}

static const buspirate_compat_callbacks_t TEST_CB = {
    .write_byte     = fix_write,
    .jtag_clock_bit = fix_clock,
    .on_exit        = fix_exit,
    .user           = NULL,
};

void setUp(void) {
    s_writes_len = 0;
    memset(s_writes, 0, sizeof(s_writes));
    s_clocks_len = 0;
    memset(s_clocks, 0, sizeof(s_clocks));
    s_tdo_script_len = 0;
    s_tdo_script_cursor = 0;
    memset(s_tdo_script, 0, sizeof(s_tdo_script));
    s_exit_count = 0;
    buspirate_compat_init(&TEST_CB);
}

void tearDown(void) {}

// Helper: feed many bytes in sequence.
static void feed(const uint8_t *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) buspirate_compat_feed_byte(bytes[i]);
}

// Helper: assert s_writes equals the given byte sequence exactly.
static void expect_writes(const uint8_t *expected, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, s_writes, n);
}

// -----------------------------------------------------------------------------
// BBIO entry
// -----------------------------------------------------------------------------

static void test_bbio_reset_replies_bbio1(void) {
    buspirate_compat_feed_byte(0x00u);
    expect_writes((const uint8_t *)"BBIO1", 5);
    TEST_ASSERT_EQUAL(BUSPIRATE_BBIO_IDLE, buspirate_compat_get_state());
}

static void test_bbio_repeated_resets_each_reply_bbio1(void) {
    // OpenOCD probes by sending 0x00 ×25 — every reset must answer
    // BBIO1, not just the first one, for the host to detect the mode.
    for (int i = 0; i < 25; i++) buspirate_compat_feed_byte(0x00u);
    TEST_ASSERT_EQUAL_size_t(25u * 5u, s_writes_len);
    for (int i = 0; i < 25; i++) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY("BBIO1", &s_writes[i * 5], 5);
    }
}

static void test_bbio_other_modes_fall_through_to_bbio1(void) {
    // We don't support 0x01..0x05 (SPI/I2C/UART/1W/RAW) so they
    // should reply BBIO1 like a fallback. Keeps the OpenOCD probe
    // loop alive even after a stray byte.
    static const uint8_t inputs[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    feed(inputs, sizeof(inputs));
    TEST_ASSERT_EQUAL_size_t(5u * 5u, s_writes_len);
    for (size_t i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL_HEX8_ARRAY("BBIO1", &s_writes[i * 5], 5);
    }
}

static void test_enter_ocd_replies_ocd1(void) {
    buspirate_compat_feed_byte(0x06u);
    expect_writes((const uint8_t *)"OCD1", 4);
    TEST_ASSERT_EQUAL(BUSPIRATE_OCD_IDLE, buspirate_compat_get_state());
}

static void test_user_term_in_bbio_calls_exit(void) {
    buspirate_compat_feed_byte(0x0Fu);
    TEST_ASSERT_EQUAL_size_t(0u, s_writes_len);
    TEST_ASSERT_EQUAL_INT(1, s_exit_count);
}

static void test_user_term_in_ocd_calls_exit(void) {
    buspirate_compat_feed_byte(0x06u);   // → OCD1
    s_writes_len = 0;
    buspirate_compat_feed_byte(0x0Fu);
    TEST_ASSERT_EQUAL_size_t(0u, s_writes_len);
    TEST_ASSERT_EQUAL_INT(1, s_exit_count);
}

// -----------------------------------------------------------------------------
// OCD sub-commands
// -----------------------------------------------------------------------------

static void test_ocd_unknown_returns_to_bbio(void) {
    // 0x06 → OCD1, then 0x00 inside OCD → BBIO1 + state back to BBIO
    static const uint8_t inputs[] = {0x06, 0x00};
    feed(inputs, sizeof(inputs));
    TEST_ASSERT_EQUAL_size_t(4u + 5u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8_ARRAY("OCD1",  &s_writes[0], 4);
    TEST_ASSERT_EQUAL_HEX8_ARRAY("BBIO1", &s_writes[4], 5);
    TEST_ASSERT_EQUAL(BUSPIRATE_BBIO_IDLE, buspirate_compat_get_state());
}

static void test_ocd_re_enter_replies_ocd1(void) {
    buspirate_compat_feed_byte(0x06u);   // → OCD1
    s_writes_len = 0;
    buspirate_compat_feed_byte(0x06u);   // re-enter inside OCD
    expect_writes((const uint8_t *)"OCD1", 4);
}

static void test_ocd_port_mode_consumes_one_arg(void) {
    static const uint8_t inputs[] = {0x06, 0x01, 0xAA};
    feed(inputs, sizeof(inputs));
    // Only OCD1 reply; no echo for port-mode.
    expect_writes((const uint8_t *)"OCD1", 4);
    TEST_ASSERT_EQUAL(BUSPIRATE_OCD_IDLE, buspirate_compat_get_state());
}

static void test_ocd_feature_consumes_two_args(void) {
    static const uint8_t inputs[] = {0x06, 0x02, 0xAA, 0xBB};
    feed(inputs, sizeof(inputs));
    expect_writes((const uint8_t *)"OCD1", 4);
    TEST_ASSERT_EQUAL(BUSPIRATE_OCD_IDLE, buspirate_compat_get_state());
}

static void test_ocd_read_adcs_replies_ten_bytes(void) {
    static const uint8_t inputs[] = {0x06, 0x03};
    feed(inputs, sizeof(inputs));
    // OCD1 (4) + [0x03, 8, 0, 0, 0, 0, 0, 0, 0, 0] (10)
    TEST_ASSERT_EQUAL_size_t(4u + 10u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0x03, s_writes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x08, s_writes[5]);
    for (size_t i = 6; i < 14; i++) TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[i]);
}

static void test_ocd_uart_speed_consumes_three_args_and_replies(void) {
    static const uint8_t inputs[] = {0x06, 0x07, 0x01, 0x02, 0x03};
    feed(inputs, sizeof(inputs));
    // OCD1 (4) + [0x07, 0x00] (2)
    TEST_ASSERT_EQUAL_size_t(4u + 2u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0x07, s_writes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[5]);
}

static void test_ocd_jtag_speed_consumes_two_args(void) {
    static const uint8_t inputs[] = {0x06, 0x08, 0x05, 0x06};
    feed(inputs, sizeof(inputs));
    expect_writes((const uint8_t *)"OCD1", 4);
}

static void test_ocd_unknown_subcmd_replies_zero(void) {
    static const uint8_t inputs[] = {0x06, 0xFFu};
    feed(inputs, sizeof(inputs));
    TEST_ASSERT_EQUAL_size_t(5u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[4]);
}

// -----------------------------------------------------------------------------
// CMD_TAP_SHIFT — the workhorse
// -----------------------------------------------------------------------------

static void test_tap_shift_empty(void) {
    static const uint8_t inputs[] = {0x06, 0x05, 0x00, 0x00};
    feed(inputs, sizeof(inputs));
    // OCD1 + [0x05, 0x00, 0x00], no clock_bit calls.
    TEST_ASSERT_EQUAL_size_t(4u + 3u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0x05, s_writes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[5]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[6]);
    TEST_ASSERT_EQUAL_size_t(0u, s_clocks_len);
    TEST_ASSERT_EQUAL(BUSPIRATE_OCD_IDLE, buspirate_compat_get_state());
}

static void test_tap_shift_4_bits(void) {
    // 4 bits, TDI=0xFA TMS=0xCC.
    // TDI 0xFA LSB-first: 0, 1, 0, 1, 1, 1, 1, 1
    // TMS 0xCC LSB-first: 0, 0, 1, 1, 0, 0, 1, 1
    // First 4 (TMS,TDI) pairs: (0,0) (0,1) (1,0) (1,1)
    // Mock TDO returns: 1, 0, 1, 0  → output byte LSB-first = 0b0101 = 0x05
    s_tdo_script[0] = true;
    s_tdo_script[1] = false;
    s_tdo_script[2] = true;
    s_tdo_script[3] = false;
    s_tdo_script_len = 4;

    static const uint8_t inputs[] = {0x06, 0x05, 0x00, 0x04, 0xFA, 0xCC};
    feed(inputs, sizeof(inputs));

    // 4 (TMS, TDI) calls.
    TEST_ASSERT_EQUAL_size_t(4u, s_clocks_len);
    TEST_ASSERT_FALSE(s_clocks[0].tms); TEST_ASSERT_FALSE(s_clocks[0].tdi);
    TEST_ASSERT_FALSE(s_clocks[1].tms); TEST_ASSERT_TRUE (s_clocks[1].tdi);
    TEST_ASSERT_TRUE (s_clocks[2].tms); TEST_ASSERT_FALSE(s_clocks[2].tdi);
    TEST_ASSERT_TRUE (s_clocks[3].tms); TEST_ASSERT_TRUE (s_clocks[3].tdi);

    // OCD1 (4) + [0x05, 0x00, 0x04, 0x05] (4) = 8 written bytes.
    TEST_ASSERT_EQUAL_size_t(4u + 4u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0x05, s_writes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[5]);
    TEST_ASSERT_EQUAL_HEX8(0x04, s_writes[6]);
    TEST_ASSERT_EQUAL_HEX8(0x05, s_writes[7]);
    TEST_ASSERT_EQUAL(BUSPIRATE_OCD_IDLE, buspirate_compat_get_state());
}

static void test_tap_shift_8_bits_full_pair(void) {
    // 8 bits, TDI=0xAA TMS=0x55.
    // Mock TDO = 0xF0 (LSB-first script: 0,0,0,0,1,1,1,1)
    bool tdo[] = {false,false,false,false, true,true,true,true};
    memcpy(s_tdo_script, tdo, sizeof(tdo));
    s_tdo_script_len = 8;

    static const uint8_t inputs[] = {0x06, 0x05, 0x00, 0x08, 0xAA, 0x55};
    feed(inputs, sizeof(inputs));

    TEST_ASSERT_EQUAL_size_t(8u, s_clocks_len);
    // TDI 0xAA LSB-first: 0,1,0,1,0,1,0,1
    // TMS 0x55 LSB-first: 1,0,1,0,1,0,1,0
    bool exp_tdi[] = {false,true,false,true,false,true,false,true};
    bool exp_tms[] = {true,false,true,false,true,false,true,false};
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL(exp_tdi[i], s_clocks[i].tdi);
        TEST_ASSERT_EQUAL(exp_tms[i], s_clocks[i].tms);
    }

    // Output: header [0x05 0x00 0x08] + 1 TDO byte = 0xF0.
    TEST_ASSERT_EQUAL_size_t(4u + 4u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0xF0, s_writes[7]);
}

static void test_tap_shift_12_bits_partial_second_pair(void) {
    // 12 bits: first pair clocks 8 bits, second pair clocks 4 bits.
    // 2 input pairs (TDI byte + TMS byte each = 4 input bytes).
    // 2 output bytes.
    bool tdo[12];
    for (size_t i = 0; i < 12; i++) tdo[i] = (i & 1u);   // alternating
    memcpy(s_tdo_script, tdo, sizeof(tdo));
    s_tdo_script_len = 12;

    static const uint8_t inputs[] = {
        0x06, 0x05, 0x00, 0x0C,
        0xFF, 0x00,   // pair 1: TDI=0xFF TMS=0x00
        0x00, 0xFF,   // pair 2: TDI=0x00 TMS=0xFF
    };
    feed(inputs, sizeof(inputs));

    TEST_ASSERT_EQUAL_size_t(12u, s_clocks_len);
    // Output: 4 (OCD1) + 3 (header) + 2 (TDO bytes) = 9.
    TEST_ASSERT_EQUAL_size_t(9u, s_writes_len);
    // First TDO byte: bits 0,1,2,3,4,5,6,7 = 0,1,0,1,0,1,0,1 → 0xAA
    TEST_ASSERT_EQUAL_HEX8(0xAA, s_writes[7]);
    // Second TDO byte: bits 0,1,2,3 of the partial pair = 0,1,0,1 → 0x0A
    TEST_ASSERT_EQUAL_HEX8(0x0A, s_writes[8]);
}

static void test_tap_shift_clamps_to_max(void) {
    // Length 0xFFFF should clamp to BUSPIRATE_TAP_SHIFT_MAX_BITS (0x2000).
    // We don't actually feed all the data — just verify the header
    // echoes back the clamped length.
    static const uint8_t inputs[] = {0x06, 0x05, 0xFFu, 0xFFu};
    feed(inputs, sizeof(inputs));
    TEST_ASSERT_EQUAL_size_t(4u + 3u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0x05, s_writes[4]);
    TEST_ASSERT_EQUAL_HEX8(0x20, s_writes[5]);   // hi byte of 0x2000
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[6]);
}

// -----------------------------------------------------------------------------
// Combined session — OpenOCD-like init sequence
// -----------------------------------------------------------------------------

static void test_full_session_init_then_exit(void) {
    // 25× 0x00 (probe) → 0x06 (enter OCD) → small TAP_SHIFT → 0x0F (exit).
    for (int i = 0; i < 25; i++) buspirate_compat_feed_byte(0x00);
    TEST_ASSERT_EQUAL_size_t(BUSPIRATE_BBIO_IDLE, buspirate_compat_get_state());

    s_writes_len = 0;
    buspirate_compat_feed_byte(0x06);
    expect_writes((const uint8_t *)"OCD1", 4);
    TEST_ASSERT_EQUAL(BUSPIRATE_OCD_IDLE, buspirate_compat_get_state());

    s_writes_len = 0;
    s_tdo_script_len = 0;   // returns false for every clock
    static const uint8_t shift[] = {0x05, 0x00, 0x05, 0x00, 0x00};
    feed(shift, sizeof(shift));
    // 5 bits, all TDO = 0 → output = [0x05 0x00 0x05 0x00].
    TEST_ASSERT_EQUAL_size_t(4u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[3]);

    buspirate_compat_feed_byte(0x0F);
    TEST_ASSERT_EQUAL_INT(1, s_exit_count);
}

// -----------------------------------------------------------------------------
// Init / get_state
// -----------------------------------------------------------------------------

static void test_init_starts_in_bbio(void) {
    buspirate_compat_init(&TEST_CB);
    TEST_ASSERT_EQUAL(BUSPIRATE_BBIO_IDLE, buspirate_compat_get_state());
}

static void test_init_null_safe(void) {
    buspirate_compat_init(NULL);    // must not crash
    // State is zeroed → BBIO_IDLE
    TEST_ASSERT_EQUAL(BUSPIRATE_BBIO_IDLE, buspirate_compat_get_state());
    // Without callbacks, feeding bytes shouldn't crash either.
    buspirate_compat_feed_byte(0x00);   // no write_byte → silent no-op
}

// -----------------------------------------------------------------------------
// Runner
// -----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_in_bbio);
    RUN_TEST(test_init_null_safe);

    RUN_TEST(test_bbio_reset_replies_bbio1);
    RUN_TEST(test_bbio_repeated_resets_each_reply_bbio1);
    RUN_TEST(test_bbio_other_modes_fall_through_to_bbio1);
    RUN_TEST(test_enter_ocd_replies_ocd1);
    RUN_TEST(test_user_term_in_bbio_calls_exit);
    RUN_TEST(test_user_term_in_ocd_calls_exit);

    RUN_TEST(test_ocd_unknown_returns_to_bbio);
    RUN_TEST(test_ocd_re_enter_replies_ocd1);
    RUN_TEST(test_ocd_port_mode_consumes_one_arg);
    RUN_TEST(test_ocd_feature_consumes_two_args);
    RUN_TEST(test_ocd_read_adcs_replies_ten_bytes);
    RUN_TEST(test_ocd_uart_speed_consumes_three_args_and_replies);
    RUN_TEST(test_ocd_jtag_speed_consumes_two_args);
    RUN_TEST(test_ocd_unknown_subcmd_replies_zero);

    RUN_TEST(test_tap_shift_empty);
    RUN_TEST(test_tap_shift_4_bits);
    RUN_TEST(test_tap_shift_8_bits_full_pair);
    RUN_TEST(test_tap_shift_12_bits_partial_second_pair);
    RUN_TEST(test_tap_shift_clamps_to_max);

    RUN_TEST(test_full_session_init_then_exit);

    return UNITY_END();
}

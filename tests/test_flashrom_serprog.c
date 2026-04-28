// Unit tests for services/flashrom_serprog — feed bytes one at a
// time, capture write_byte / spi_cs_set / spi_xfer_byte / yield /
// on_exit calls into fixtures, assert the serprog v1 protocol shape.

#include "unity.h"

#include <string.h>

#include "flashrom_serprog.h"

#define S_ACK 0x06u
#define S_NAK 0x15u

// -----------------------------------------------------------------------------
// Test fixtures
// -----------------------------------------------------------------------------

#define MAX_WRITES 4096u
#define MAX_XFER   1024u

static uint8_t s_writes[MAX_WRITES];
static size_t  s_writes_len;

static uint8_t s_xfer_log[MAX_XFER];   // bytes we sent on MOSI
static size_t  s_xfer_len;
static uint8_t s_miso_script[MAX_XFER];
static size_t  s_miso_script_len;
static size_t  s_miso_cursor;

static int     s_cs_low_calls;
static int     s_cs_high_calls;
static int     s_yield_calls;
static int     s_exit_calls;

static void fix_write(uint8_t b, void *u) {
    (void)u;
    if (s_writes_len < MAX_WRITES) s_writes[s_writes_len++] = b;
}

static void fix_cs(bool low, void *u) {
    (void)u;
    if (low)  s_cs_low_calls++;
    else      s_cs_high_calls++;
}

static uint8_t fix_xfer(uint8_t out, void *u) {
    (void)u;
    if (s_xfer_len < MAX_XFER) s_xfer_log[s_xfer_len++] = out;
    if (s_miso_cursor < s_miso_script_len) {
        return s_miso_script[s_miso_cursor++];
    }
    return 0xFFu;
}

static void fix_yield(void *u)   { (void)u; s_yield_calls++; }
static void fix_exit(void *u)    { (void)u; s_exit_calls++; }

static const flashrom_serprog_callbacks_t TEST_CB = {
    .write_byte    = fix_write,
    .spi_cs_set    = fix_cs,
    .spi_xfer_byte = fix_xfer,
    .yield         = fix_yield,
    .on_exit       = fix_exit,
    .user          = NULL,
};

void setUp(void) {
    s_writes_len = 0;
    memset(s_writes, 0, sizeof(s_writes));
    s_xfer_len = 0;
    memset(s_xfer_log, 0, sizeof(s_xfer_log));
    s_miso_script_len = 0;
    s_miso_cursor = 0;
    memset(s_miso_script, 0, sizeof(s_miso_script));
    s_cs_low_calls = 0;
    s_cs_high_calls = 0;
    s_yield_calls = 0;
    s_exit_calls = 0;
    flashrom_serprog_init(&TEST_CB);
}

void tearDown(void) {}

static void feed(const uint8_t *bytes, size_t n) {
    for (size_t i = 0; i < n; i++) flashrom_serprog_feed_byte(bytes[i]);
}

// -----------------------------------------------------------------------------
// Init / state
// -----------------------------------------------------------------------------

static void test_init_starts_in_idle(void) {
    TEST_ASSERT_EQUAL(FLASHROM_SP_IDLE, flashrom_serprog_get_state());
}

static void test_init_null_safe(void) {
    flashrom_serprog_init(NULL);
    TEST_ASSERT_EQUAL(FLASHROM_SP_IDLE, flashrom_serprog_get_state());
    flashrom_serprog_feed_byte(0x00u);  // must not crash without callbacks
}

// -----------------------------------------------------------------------------
// Query commands
// -----------------------------------------------------------------------------

static void test_nop(void) {
    flashrom_serprog_feed_byte(0x00);
    TEST_ASSERT_EQUAL_size_t(1u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
}

static void test_q_iface(void) {
    flashrom_serprog_feed_byte(0x01);
    TEST_ASSERT_EQUAL_size_t(3u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01,  s_writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00,  s_writes[2]);
}

static void test_q_cmdmap_layout(void) {
    flashrom_serprog_feed_byte(0x02);
    // ACK + 32-byte bitmap.
    TEST_ASSERT_EQUAL_size_t(33u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    // byte 0 — cmds 0..7 supported: NOP, Q_IFACE, Q_CMDMAP, Q_PGMNAME,
    // Q_SERBUF, Q_BUSTYPE → bits 0..5 = 0x3F.
    TEST_ASSERT_EQUAL_HEX8(0x3F, s_writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[2]);
    // byte 2 — cmds 0x10..0x17: SYNCNOP, S_BUSTYPE, O_SPIOP, S_SPI_FREQ,
    // S_PIN_STATE → bits 0,2,3,4,5 = 0x3D.
    TEST_ASSERT_EQUAL_HEX8(0x3D, s_writes[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[4]);
    // remaining 28 bytes all zero.
    for (size_t i = 5; i < 33; i++) TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[i]);
}

static void test_q_pgmname(void) {
    flashrom_serprog_feed_byte(0x03);
    // ACK + 16-byte name "FaultyCat\0\0\0\0\0\0\0".
    TEST_ASSERT_EQUAL_size_t(17u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY("FaultyCat", &s_writes[1], 9);
    for (size_t i = 10; i < 17; i++) TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[i]);
}

static void test_q_serbuf(void) {
    flashrom_serprog_feed_byte(0x04);
    TEST_ASSERT_EQUAL_size_t(3u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, s_writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, s_writes[2]);
}

static void test_q_bustype_reports_spi(void) {
    flashrom_serprog_feed_byte(0x05);
    TEST_ASSERT_EQUAL_size_t(2u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8(FLASHROM_SERPROG_BUSTYPE_SPI, s_writes[1]);
}

static void test_syncnop(void) {
    flashrom_serprog_feed_byte(0x10);
    TEST_ASSERT_EQUAL_size_t(2u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_NAK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[1]);
}

// -----------------------------------------------------------------------------
// S_BUSTYPE
// -----------------------------------------------------------------------------

static void test_s_bustype_spi_acks(void) {
    static const uint8_t in[] = {0x12, 0x08};
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_size_t(1u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
}

static void test_s_bustype_parallel_naks(void) {
    static const uint8_t in[] = {0x12, 0x01};   // PARALLEL bit
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_size_t(1u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_NAK, s_writes[0]);
}

static void test_s_bustype_spi_plus_others_acks(void) {
    // 0x0A = SPI(0x08) | LPC(0x02). ACK because SPI is set.
    static const uint8_t in[] = {0x12, 0x0A};
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
}

// -----------------------------------------------------------------------------
// S_SPI_FREQ
// -----------------------------------------------------------------------------

static void test_s_spi_freq_zero_naks(void) {
    static const uint8_t in[] = {0x14, 0, 0, 0, 0};
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_size_t(1u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_NAK, s_writes[0]);
}

static void test_s_spi_freq_nonzero_acks_with_actual(void) {
    // Request 4 MHz; we always report 1 MHz back regardless.
    static const uint8_t in[] = {0x14, 0x00, 0x09, 0x3D, 0x00};   // 4_000_000 LE
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_size_t(5u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    // 1_000_000 LE = 0x40 0x42 0x0F 0x00.
    TEST_ASSERT_EQUAL_HEX8(0x40, s_writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x42, s_writes[2]);
    TEST_ASSERT_EQUAL_HEX8(0x0F, s_writes[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_writes[4]);
}

// -----------------------------------------------------------------------------
// S_PIN_STATE
// -----------------------------------------------------------------------------

static void test_s_pin_state_acks(void) {
    static const uint8_t in[] = {0x15, 0x01};
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
}

// -----------------------------------------------------------------------------
// O_SPIOP — the workhorse
// -----------------------------------------------------------------------------

static void test_spiop_read_only(void) {
    // wlen=0, rlen=4. CS asserts low, S_ACK, 4 read bytes.
    s_miso_script[0] = 0xDE;
    s_miso_script[1] = 0xAD;
    s_miso_script[2] = 0xBE;
    s_miso_script[3] = 0xEF;
    s_miso_script_len = 4;

    static const uint8_t in[] = {
        0x13,
        0x00, 0x00, 0x00,   // wlen = 0
        0x04, 0x00, 0x00,   // rlen = 4
    };
    feed(in, sizeof(in));

    TEST_ASSERT_EQUAL_INT(1, s_cs_low_calls);
    TEST_ASSERT_EQUAL_INT(1, s_cs_high_calls);
    TEST_ASSERT_EQUAL_size_t(4u, s_xfer_len);
    // Output: ACK + 4 MISO bytes.
    TEST_ASSERT_EQUAL_size_t(5u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xDE,  s_writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAD,  s_writes[2]);
    TEST_ASSERT_EQUAL_HEX8(0xBE,  s_writes[3]);
    TEST_ASSERT_EQUAL_HEX8(0xEF,  s_writes[4]);
}

static void test_spiop_write_only(void) {
    // wlen=2, rlen=0. CS low, 2 xfer (write phase, MISO discarded),
    // ACK, no read xfer, CS high.
    static const uint8_t in[] = {
        0x13,
        0x02, 0x00, 0x00,   // wlen = 2
        0x00, 0x00, 0x00,   // rlen = 0
        0x9F, 0x90,         // wbytes (e.g. RDID + dummy)
    };
    feed(in, sizeof(in));

    TEST_ASSERT_EQUAL_INT(1, s_cs_low_calls);
    TEST_ASSERT_EQUAL_INT(1, s_cs_high_calls);
    TEST_ASSERT_EQUAL_size_t(2u, s_xfer_len);
    TEST_ASSERT_EQUAL_HEX8(0x9F, s_xfer_log[0]);
    TEST_ASSERT_EQUAL_HEX8(0x90, s_xfer_log[1]);
    TEST_ASSERT_EQUAL_size_t(1u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
}

static void test_spiop_write_then_read(void) {
    // Typical 25-series JEDEC ID query: write 1 byte (0x9F = RDID),
    // read 3 bytes (mfg + dev hi + dev lo).
    //
    // The fixture's spi_xfer_byte advances the MISO cursor on EVERY
    // call — including write-phase calls where the firmware
    // discards MISO. That mirrors real SPI bit-bang where MISO is
    // sampled bit-by-bit regardless of which side "owns" the data
    // direction. So the script needs an extra dummy byte at slot 0
    // that gets consumed (and discarded) by the write phase.
    s_miso_script[0] = 0xAA;   // discarded during write phase (1 wbyte)
    s_miso_script[1] = 0xEF;   // mfg = Winbond
    s_miso_script[2] = 0x40;   // dev hi
    s_miso_script[3] = 0x16;   // dev lo (W25Q32)
    s_miso_script_len = 4;

    static const uint8_t in[] = {
        0x13,
        0x01, 0x00, 0x00,   // wlen = 1
        0x03, 0x00, 0x00,   // rlen = 3
        0x9Fu,              // RDID
    };
    feed(in, sizeof(in));

    TEST_ASSERT_EQUAL_INT(1, s_cs_low_calls);
    TEST_ASSERT_EQUAL_INT(1, s_cs_high_calls);
    // 1 write xfer + 3 read xfers = 4 total.
    TEST_ASSERT_EQUAL_size_t(4u, s_xfer_len);
    TEST_ASSERT_EQUAL_HEX8(0x9F, s_xfer_log[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_xfer_log[1]);   // read phase MOSI = 0
    TEST_ASSERT_EQUAL_HEX8(0x00, s_xfer_log[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, s_xfer_log[3]);
    // Output: ACK + 3 MISO bytes.
    TEST_ASSERT_EQUAL_size_t(4u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, s_writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x40, s_writes[2]);
    TEST_ASSERT_EQUAL_HEX8(0x16, s_writes[3]);
}

static void test_spiop_zero_zero_just_pulses_cs(void) {
    static const uint8_t in[] = {
        0x13,
        0x00, 0x00, 0x00,   // wlen = 0
        0x00, 0x00, 0x00,   // rlen = 0
    };
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_INT(1, s_cs_low_calls);
    TEST_ASSERT_EQUAL_INT(1, s_cs_high_calls);
    TEST_ASSERT_EQUAL_size_t(0u, s_xfer_len);
    TEST_ASSERT_EQUAL_size_t(1u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
}

static void test_spiop_yield_called_during_long_read(void) {
    // 256 read bytes — yield should fire ~2 times (every 128 bytes).
    s_miso_script_len = 0;   // returns 0xFF default
    static const uint8_t in[] = {
        0x13,
        0x00, 0x00, 0x00,
        0x00, 0x01, 0x00,   // rlen = 256
    };
    feed(in, sizeof(in));
    TEST_ASSERT_EQUAL_size_t(256u, s_xfer_len);
    // 256 bytes / 128 yield-period = 2 expected yields.
    TEST_ASSERT_EQUAL_INT(2, s_yield_calls);
}

static void test_spiop_yield_called_during_long_write(void) {
    // 256 write bytes — yield should fire ~2 times.
    static const uint8_t header[] = {
        0x13,
        0x00, 0x01, 0x00,   // wlen = 256
        0x00, 0x00, 0x00,
    };
    feed(header, sizeof(header));
    for (uint32_t i = 0; i < 256u; i++) {
        flashrom_serprog_feed_byte((uint8_t)(i & 0xFFu));
    }
    TEST_ASSERT_EQUAL_size_t(256u, s_xfer_len);
    TEST_ASSERT_EQUAL_INT(2, s_yield_calls);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
}

// -----------------------------------------------------------------------------
// Unknown commands NAK
// -----------------------------------------------------------------------------

static void test_unknown_cmd_naks(void) {
    flashrom_serprog_feed_byte(0xAAu);
    TEST_ASSERT_EQUAL_size_t(1u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_NAK, s_writes[0]);
}

static void test_chipsize_naks(void) {
    // Q_CHIPSIZE (0x06) — we don't support it; should NAK.
    flashrom_serprog_feed_byte(0x06u);
    TEST_ASSERT_EQUAL_HEX8(S_NAK, s_writes[0]);
}

static void test_o_init_naks(void) {
    flashrom_serprog_feed_byte(0x0Bu);
    TEST_ASSERT_EQUAL_HEX8(S_NAK, s_writes[0]);
}

// -----------------------------------------------------------------------------
// Combined session — simulates flashrom's startup probe
// -----------------------------------------------------------------------------

static void test_full_session_handshake_then_rdid(void) {
    // SYNCNOP → Q_IFACE → Q_CMDMAP → Q_PGMNAME → Q_SERBUF →
    // Q_BUSTYPE → S_BUSTYPE(SPI) → S_SPI_FREQ → S_PIN_STATE →
    // SPIOP RDID.
    static const uint8_t prelude[] = {
        0x10,                                 // SYNCNOP
        0x01,                                 // Q_IFACE
        0x02,                                 // Q_CMDMAP
        0x03,                                 // Q_PGMNAME
        0x04,                                 // Q_SERBUF
        0x05,                                 // Q_BUSTYPE
        0x12, 0x08,                           // S_BUSTYPE SPI
        0x14, 0x40, 0x42, 0x0F, 0x00,         // S_SPI_FREQ 1 MHz
        0x15, 0x01,                           // S_PIN_STATE = 1 (enable drivers)
    };
    feed(prelude, sizeof(prelude));

    // The crucial bit — server should not have called CS yet (no SPIOP).
    TEST_ASSERT_EQUAL_INT(0, s_cs_low_calls);

    // Now JEDEC ID query. Dummy byte at slot 0 to absorb the write
    // phase's discarded MISO (see test_spiop_write_then_read for the
    // reasoning).
    s_miso_script[0] = 0xAA;   // discarded during write phase
    s_miso_script[1] = 0xC2;   // Macronix
    s_miso_script[2] = 0x20;
    s_miso_script[3] = 0x16;   // MX25L3206E
    s_miso_script_len = 4;

    s_writes_len = 0;
    s_xfer_len = 0;

    static const uint8_t rdid[] = {
        0x13,
        0x01, 0x00, 0x00,
        0x03, 0x00, 0x00,
        0x9Fu,
    };
    feed(rdid, sizeof(rdid));

    TEST_ASSERT_EQUAL_INT(1, s_cs_low_calls);
    TEST_ASSERT_EQUAL_INT(1, s_cs_high_calls);
    TEST_ASSERT_EQUAL_size_t(4u, s_writes_len);
    TEST_ASSERT_EQUAL_HEX8(S_ACK, s_writes[0]);
    TEST_ASSERT_EQUAL_HEX8(0xC2, s_writes[1]);
    TEST_ASSERT_EQUAL_HEX8(0x20, s_writes[2]);
    TEST_ASSERT_EQUAL_HEX8(0x16, s_writes[3]);
}

// -----------------------------------------------------------------------------
// Runner
// -----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_in_idle);
    RUN_TEST(test_init_null_safe);

    RUN_TEST(test_nop);
    RUN_TEST(test_q_iface);
    RUN_TEST(test_q_cmdmap_layout);
    RUN_TEST(test_q_pgmname);
    RUN_TEST(test_q_serbuf);
    RUN_TEST(test_q_bustype_reports_spi);
    RUN_TEST(test_syncnop);

    RUN_TEST(test_s_bustype_spi_acks);
    RUN_TEST(test_s_bustype_parallel_naks);
    RUN_TEST(test_s_bustype_spi_plus_others_acks);

    RUN_TEST(test_s_spi_freq_zero_naks);
    RUN_TEST(test_s_spi_freq_nonzero_acks_with_actual);

    RUN_TEST(test_s_pin_state_acks);

    RUN_TEST(test_spiop_read_only);
    RUN_TEST(test_spiop_write_only);
    RUN_TEST(test_spiop_write_then_read);
    RUN_TEST(test_spiop_zero_zero_just_pulses_cs);
    RUN_TEST(test_spiop_yield_called_during_long_read);
    RUN_TEST(test_spiop_yield_called_during_long_write);

    RUN_TEST(test_unknown_cmd_naks);
    RUN_TEST(test_chipsize_naks);
    RUN_TEST(test_o_init_naks);

    RUN_TEST(test_full_session_handshake_then_rdid);

    return UNITY_END();
}

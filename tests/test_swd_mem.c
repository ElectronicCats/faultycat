// Unit tests for services/swd_core/swd_mem — drives swd_dp + swd_phy
// through the hal_fake_pio FIFO and pre-populates RX with the bit
// pattern the target would push. Verifies CSW init, MEM-AP read32
// pipelined-via-RDBUFF, and MEM-AP write32 sequences.

#include "unity.h"

#include "board_v2.h"
#include "hal/pio.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "swd_dp.h"
#include "swd_mem.h"
#include "swd_phy.h"

#define PIO1 1u
#define SM0  0u

static const uint8_t SWCLK = BOARD_GP_SCANNER_CH0;
static const uint8_t SWDIO = BOARD_GP_SCANNER_CH1;

void setUp(void) {
    hal_fake_pio_reset();
    hal_fake_gpio_reset();
    swd_phy_init(SWCLK, SWDIO, SWD_PHY_NRST_NONE);
    hal_fake_pio_insts[PIO1].sm[SM0].tx_count = 0;
}

void tearDown(void) {
    swd_phy_deinit();
}

// -----------------------------------------------------------------------------
// RX helpers — see test_swd_dp.c for the encoding rationale.
// -----------------------------------------------------------------------------

static uint32_t isr_for(uint32_t value, uint32_t bit_count) {
    return value << (32u - bit_count);
}

static void push_ack(swd_dp_ack_t a) {
    hal_fake_pio_push_rx(PIO1, SM0, isr_for((uint32_t)a, 3u));
}
static void push_data32(uint32_t v) {
    hal_fake_pio_push_rx(PIO1, SM0, v);
}
static void push_parity(uint8_t p) {
    hal_fake_pio_push_rx(PIO1, SM0, isr_for(p & 1u, 1u));
}
static void push_read_response(swd_dp_ack_t a, uint32_t v) {
    push_ack(a);
    if (a == SWD_ACK_OK) {
        push_data32(v);
        push_parity(swd_dp_compute_parity(v));
    }
}

// Walk the TX FIFO and collect every entry that immediately follows
// a count=8 command — those are the SWD request bytes the host emit-
// ted, in order. Returns the number collected.
static uint32_t collect_request_bytes(uint8_t *out, uint32_t cap) {
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    uint32_t n = 0;
    for (uint32_t i = 0; i + 1 < sm->tx_count && n < cap; i++) {
        uint32_t bit_count = (sm->tx_fifo[i] & 0xFFu) + 1u;
        if (bit_count == 8u) {
            out[n++] = (uint8_t)sm->tx_fifo[i + 1];
        }
    }
    return n;
}

// Expected SWD request bytes for the operations swd_mem performs.
//   DP write SELECT (addr 0x08): 0xB1
//   AP write CSW    (addr 0x00): 0xA3
//   AP write TAR    (addr 0x04): 0x8B
//   AP read  DRW    (addr 0x0C): 0x9F
//   AP write DRW    (addr 0x0C): 0xBB
//   DP read  RDBUFF (addr 0x0C): 0xBD
#define REQ_DP_WRITE_SELECT  0xB1u
#define REQ_AP_WRITE_CSW     0xA3u
#define REQ_AP_WRITE_TAR     0x8Bu
#define REQ_AP_READ_DRW      0x9Fu
#define REQ_AP_WRITE_DRW     0xBBu
#define REQ_DP_READ_RDBUFF   0xBDu

// -----------------------------------------------------------------------------
// init
// -----------------------------------------------------------------------------

static void test_init_writes_select_then_csw(void) {
    push_ack(SWD_ACK_OK);  // SELECT write ack
    push_ack(SWD_ACK_OK);  // CSW write ack
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_mem_init());
    uint8_t reqs[8];
    uint32_t n = collect_request_bytes(reqs, 8u);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2u, n);
    TEST_ASSERT_EQUAL_HEX8(REQ_DP_WRITE_SELECT, reqs[0]);
    TEST_ASSERT_EQUAL_HEX8(REQ_AP_WRITE_CSW,    reqs[1]);
}

static void test_init_propagates_select_failure(void) {
    push_ack(SWD_ACK_FAULT);
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_mem_init());
}

static void test_init_propagates_csw_failure(void) {
    push_ack(SWD_ACK_OK);     // SELECT ok
    push_ack(SWD_ACK_WAIT);   // CSW wait
    TEST_ASSERT_EQUAL(SWD_ACK_WAIT, swd_mem_init());
}

// -----------------------------------------------------------------------------
// read32
// -----------------------------------------------------------------------------

static void test_read32_emits_tar_then_drw_then_rdbuff(void) {
    push_ack(SWD_ACK_OK);                                // TAR write
    push_read_response(SWD_ACK_OK, 0xCAFEBABEu);         // AP read DRW (garbage discarded)
    push_read_response(SWD_ACK_OK, 0xDEADBEEFu);         // RDBUFF read (real data)
    uint32_t v = 0u;
    TEST_ASSERT_EQUAL(SWD_ACK_OK, swd_mem_read32(0x20000000u, &v));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, v);
    uint8_t reqs[8];
    uint32_t n = collect_request_bytes(reqs, 8u);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(3u, n);
    TEST_ASSERT_EQUAL_HEX8(REQ_AP_WRITE_TAR,    reqs[0]);
    TEST_ASSERT_EQUAL_HEX8(REQ_AP_READ_DRW,     reqs[1]);
    TEST_ASSERT_EQUAL_HEX8(REQ_DP_READ_RDBUFF,  reqs[2]);
}

static void test_read32_writes_tar_with_requested_address(void) {
    push_ack(SWD_ACK_OK);
    push_read_response(SWD_ACK_OK, 0u);
    push_read_response(SWD_ACK_OK, 0u);
    uint32_t v = 0u;
    swd_mem_read32(0xE000ED00u, &v);   // SCB->CPUID address
    // The TAR data word is the 32-bit entry that follows the
    // (count=32, dir=1) command immediately after the AP_WRITE_TAR
    // request byte.
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[PIO1].sm[SM0];
    bool found_tar = false;
    for (uint32_t i = 0; i + 3 < sm->tx_count; i++) {
        if ((sm->tx_fifo[i] & 0xFFu) + 1u == 8u
         && (uint8_t)sm->tx_fifo[i + 1] == REQ_AP_WRITE_TAR) {
            // Walk forward to the count=32 with dir=1 entry.
            for (uint32_t j = i + 2; j + 1 < sm->tx_count; j++) {
                uint32_t bc = (sm->tx_fifo[j] & 0xFFu) + 1u;
                bool dir   = (sm->tx_fifo[j] >> 8) & 1u;
                if (bc == 32u && dir) {
                    TEST_ASSERT_EQUAL_HEX32(0xE000ED00u, sm->tx_fifo[j + 1]);
                    found_tar = true;
                    break;
                }
            }
            break;
        }
    }
    TEST_ASSERT_TRUE(found_tar);
}

static void test_read32_propagates_tar_failure(void) {
    push_ack(SWD_ACK_FAULT);
    uint32_t v = 0xAAu;
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_mem_read32(0u, &v));
    TEST_ASSERT_EQUAL_HEX32(0xAAu, v);
}

static void test_read32_propagates_drw_failure(void) {
    push_ack(SWD_ACK_OK);          // TAR ok
    push_ack(SWD_ACK_WAIT);        // AP read DRW WAIT — short-circuits before parity
    uint32_t v = 0xBBu;
    TEST_ASSERT_EQUAL(SWD_ACK_WAIT, swd_mem_read32(0u, &v));
    TEST_ASSERT_EQUAL_HEX32(0xBBu, v);
}

static void test_read32_propagates_rdbuff_failure(void) {
    push_ack(SWD_ACK_OK);                          // TAR ok
    push_read_response(SWD_ACK_OK, 0u);            // DRW garbage
    push_ack(SWD_ACK_FAULT);                       // RDBUFF FAULT
    uint32_t v = 0xCCu;
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_mem_read32(0u, &v));
    TEST_ASSERT_EQUAL_HEX32(0xCCu, v);
}

// -----------------------------------------------------------------------------
// write32
// -----------------------------------------------------------------------------

static void test_write32_emits_tar_then_drw(void) {
    push_ack(SWD_ACK_OK);
    push_ack(SWD_ACK_OK);
    TEST_ASSERT_EQUAL(SWD_ACK_OK,
        swd_mem_write32(0x20001000u, 0xFEEDC0DEu));
    uint8_t reqs[8];
    uint32_t n = collect_request_bytes(reqs, 8u);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(2u, n);
    TEST_ASSERT_EQUAL_HEX8(REQ_AP_WRITE_TAR, reqs[0]);
    TEST_ASSERT_EQUAL_HEX8(REQ_AP_WRITE_DRW, reqs[1]);
}

static void test_write32_propagates_tar_failure(void) {
    push_ack(SWD_ACK_FAULT);
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_mem_write32(0u, 0u));
}

static void test_write32_propagates_drw_failure(void) {
    push_ack(SWD_ACK_OK);
    push_ack(SWD_ACK_FAULT);
    TEST_ASSERT_EQUAL(SWD_ACK_FAULT, swd_mem_write32(0u, 0xDEADu));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_writes_select_then_csw);
    RUN_TEST(test_init_propagates_select_failure);
    RUN_TEST(test_init_propagates_csw_failure);
    RUN_TEST(test_read32_emits_tar_then_drw_then_rdbuff);
    RUN_TEST(test_read32_writes_tar_with_requested_address);
    RUN_TEST(test_read32_propagates_tar_failure);
    RUN_TEST(test_read32_propagates_drw_failure);
    RUN_TEST(test_read32_propagates_rdbuff_failure);
    RUN_TEST(test_write32_emits_tar_then_drw);
    RUN_TEST(test_write32_propagates_tar_failure);
    RUN_TEST(test_write32_propagates_drw_failure);
    return UNITY_END();
}

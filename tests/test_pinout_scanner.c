// Unit tests for services/pinout_scanner — focus is the pure
// permutation iterator. End-to-end pinout_scan_jtag / pinout_scan_swd
// runs are deferred to physical smoke (a target with a known IDCODE
// or DPIDR) since each iteration tears down + re-inits the phy and
// the hal_fake_gpio input scripts are per-pin → re-loading them every
// iteration is more bookkeeping than the test value warrants.

#include "unity.h"

#include <string.h>

#include "pinout_scanner.h"

void setUp(void)    {}
void tearDown(void) {}

// -----------------------------------------------------------------------------
// pinout_perm_total
// -----------------------------------------------------------------------------

static void test_perm_total_canonical_cases(void) {
    // P(8, 4) — full FaultyCat v2.x scanner JTAG search
    TEST_ASSERT_EQUAL_UINT32(1680u, pinout_perm_total(4, 8));
    // P(8, 2) — SWD search
    TEST_ASSERT_EQUAL_UINT32(56u,   pinout_perm_total(2, 8));
    // P(16, 4) — blueTag standalone JTAG
    TEST_ASSERT_EQUAL_UINT32(43680u, pinout_perm_total(4, 16));
    // P(4, 4) = 4!
    TEST_ASSERT_EQUAL_UINT32(24u,   pinout_perm_total(4, 4));
    TEST_ASSERT_EQUAL_UINT32(12u,   pinout_perm_total(2, 4));
}

static void test_perm_total_edge_cases(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, pinout_perm_total(0, 8));    // k == 0 → undefined
    TEST_ASSERT_EQUAL_UINT32(0u, pinout_perm_total(5, 4));    // k > n → 0
    TEST_ASSERT_EQUAL_UINT32(8u, pinout_perm_total(1, 8));    // P(n, 1) == n
}

// -----------------------------------------------------------------------------
// pinout_perm_next
// -----------------------------------------------------------------------------

static void test_perm_next_count_matches_total_4_8(void) {
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 4, 8);

    uint32_t count = 0u;
    while (pinout_perm_next(&it)) count++;
    TEST_ASSERT_EQUAL_UINT32(1680u, count);
}

static void test_perm_next_count_matches_total_2_8(void) {
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 2, 8);

    uint32_t count = 0u;
    while (pinout_perm_next(&it)) count++;
    TEST_ASSERT_EQUAL_UINT32(56u, count);
}

static void test_perm_next_count_matches_total_4_4(void) {
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 4, 4);

    uint32_t count = 0u;
    while (pinout_perm_next(&it)) count++;
    TEST_ASSERT_EQUAL_UINT32(24u, count);   // 4!
}

static void test_perm_next_first_tuple_is_lex_smallest(void) {
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 4, 8);

    TEST_ASSERT_TRUE(pinout_perm_next(&it));
    TEST_ASSERT_EQUAL_UINT8(0u, it.indices[0]);
    TEST_ASSERT_EQUAL_UINT8(1u, it.indices[1]);
    TEST_ASSERT_EQUAL_UINT8(2u, it.indices[2]);
    TEST_ASSERT_EQUAL_UINT8(3u, it.indices[3]);
}

static void test_perm_next_all_tuples_unique_and_within_range(void) {
    // For (k=2, n=4) walk every tuple and verify each is unique
    // (no dupes within), values are 0..n-1, and the SET of tuples
    // generated is also unique (no tuple repeats).
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 2, 4);

    uint8_t seen[4 * 4] = {0};   // bitmap of (i*n + j) tuples seen
    uint32_t count = 0u;
    while (pinout_perm_next(&it)) {
        TEST_ASSERT_NOT_EQUAL(it.indices[0], it.indices[1]);
        TEST_ASSERT_TRUE(it.indices[0] < 4);
        TEST_ASSERT_TRUE(it.indices[1] < 4);
        size_t key = (size_t)it.indices[0] * 4u + (size_t)it.indices[1];
        TEST_ASSERT_FALSE_MESSAGE(seen[key], "tuple repeated");
        seen[key] = 1u;
        count++;
    }
    TEST_ASSERT_EQUAL_UINT32(12u, count);
}

static void test_perm_next_iter_in_lex_order(void) {
    // For (k=2, n=3) the lex order is:
    //   (0,1) (0,2) (1,0) (1,2) (2,0) (2,1)
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 2, 3);

    static const uint8_t expected[6][2] = {
        {0,1}, {0,2}, {1,0}, {1,2}, {2,0}, {2,1},
    };
    for (uint32_t i = 0; i < 6u; i++) {
        TEST_ASSERT_TRUE(pinout_perm_next(&it));
        TEST_ASSERT_EQUAL_UINT8(expected[i][0], it.indices[0]);
        TEST_ASSERT_EQUAL_UINT8(expected[i][1], it.indices[1]);
    }
    TEST_ASSERT_FALSE(pinout_perm_next(&it));
}

static void test_perm_next_handles_k_eq_n(void) {
    // k == n means "permutations of all elements" — should yield n!.
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 3, 3);

    uint32_t count = 0u;
    while (pinout_perm_next(&it)) count++;
    TEST_ASSERT_EQUAL_UINT32(6u, count);   // 3!
}

static void test_perm_next_rejects_k_gt_n(void) {
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 5, 4);
    TEST_ASSERT_FALSE(pinout_perm_next(&it));
}

static void test_perm_next_rejects_k_zero(void) {
    pinout_perm_iter_t it;
    pinout_perm_init(&it, 0, 8);
    TEST_ASSERT_FALSE(pinout_perm_next(&it));
}

static void test_perm_init_null_safe(void) {
    pinout_perm_init(NULL, 4, 8);   // must not crash
    TEST_ASSERT_FALSE(pinout_perm_next(NULL));
}

// -----------------------------------------------------------------------------
// Constants — sanity-check the canonical macros match the iterator
// -----------------------------------------------------------------------------

static void test_constants_match_iterator_total(void) {
    TEST_ASSERT_EQUAL_UINT32(PINOUT_SCANNER_JTAG_TOTAL,
                             pinout_perm_total(PINOUT_SCANNER_JTAG_PINS,
                                               PINOUT_SCANNER_CHANNELS));
    TEST_ASSERT_EQUAL_UINT32(PINOUT_SCANNER_SWD_TOTAL,
                             pinout_perm_total(PINOUT_SCANNER_SWD_PINS,
                                               PINOUT_SCANNER_CHANNELS));
}

// -----------------------------------------------------------------------------
// Runner
// -----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_perm_total_canonical_cases);
    RUN_TEST(test_perm_total_edge_cases);

    RUN_TEST(test_perm_next_count_matches_total_4_8);
    RUN_TEST(test_perm_next_count_matches_total_2_8);
    RUN_TEST(test_perm_next_count_matches_total_4_4);
    RUN_TEST(test_perm_next_first_tuple_is_lex_smallest);
    RUN_TEST(test_perm_next_all_tuples_unique_and_within_range);
    RUN_TEST(test_perm_next_iter_in_lex_order);
    RUN_TEST(test_perm_next_handles_k_eq_n);
    RUN_TEST(test_perm_next_rejects_k_gt_n);
    RUN_TEST(test_perm_next_rejects_k_zero);
    RUN_TEST(test_perm_init_null_safe);

    RUN_TEST(test_constants_match_iterator_total);

    return UNITY_END();
}

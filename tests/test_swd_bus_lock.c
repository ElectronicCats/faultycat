// Unit tests for services/swd_bus_lock — exercises the F9-1
// service-layer mutex covering the shared scanner-header bus.

#include "unity.h"

#include "swd_bus_lock.h"

void setUp(void) {
    swd_bus_lock_init();
}

void tearDown(void) {}

// -----------------------------------------------------------------------------
// Init / state
// -----------------------------------------------------------------------------

static void test_init_starts_idle(void) {
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_IDLE, swd_bus_owner());
    TEST_ASSERT_FALSE(swd_bus_is_held());
}

static void test_init_clears_held_state(void) {
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    TEST_ASSERT_TRUE(swd_bus_is_held());
    swd_bus_lock_init();
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_IDLE, swd_bus_owner());
    TEST_ASSERT_FALSE(swd_bus_is_held());
}

// -----------------------------------------------------------------------------
// Acquire / release happy path
// -----------------------------------------------------------------------------

static void test_acquire_when_idle_succeeds(void) {
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_CAMPAIGN, swd_bus_owner());
    TEST_ASSERT_TRUE(swd_bus_is_held());
}

static void test_release_returns_to_idle(void) {
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_SCANNER));
    swd_bus_release(SWD_BUS_OWNER_SCANNER);
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_IDLE, swd_bus_owner());
    TEST_ASSERT_FALSE(swd_bus_is_held());
}

static void test_acquire_after_release_succeeds(void) {
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_SCANNER));
    swd_bus_release(SWD_BUS_OWNER_SCANNER);
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_CAMPAIGN, swd_bus_owner());
}

// -----------------------------------------------------------------------------
// Contention rules
// -----------------------------------------------------------------------------

static void test_acquire_when_held_returns_false(void) {
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    TEST_ASSERT_FALSE(swd_bus_try_acquire(SWD_BUS_OWNER_DAPLINK));
    // Owner unchanged.
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_CAMPAIGN, swd_bus_owner());
}

static void test_double_acquire_same_owner_rejects(void) {
    // No re-entrance — surfacing accidental double-acquires is more
    // useful than silently allowing them.
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    TEST_ASSERT_FALSE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_CAMPAIGN, swd_bus_owner());
}

static void test_acquire_as_idle_rejects(void) {
    TEST_ASSERT_FALSE(swd_bus_try_acquire(SWD_BUS_OWNER_IDLE));
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_IDLE, swd_bus_owner());
    TEST_ASSERT_FALSE(swd_bus_is_held());
}

// -----------------------------------------------------------------------------
// Release semantics
// -----------------------------------------------------------------------------

static void test_release_when_idle_is_safe(void) {
    swd_bus_release(SWD_BUS_OWNER_CAMPAIGN);   // wrong owner — no-op
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_IDLE, swd_bus_owner());
    TEST_ASSERT_FALSE(swd_bus_is_held());
}

static void test_release_wrong_owner_no_op(void) {
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    swd_bus_release(SWD_BUS_OWNER_SCANNER);    // wrong owner
    // Bus stays held by CAMPAIGN — wrong-owner release is silent.
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_CAMPAIGN, swd_bus_owner());
    TEST_ASSERT_TRUE(swd_bus_is_held());
}

static void test_double_release_safe(void) {
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_DAPLINK));
    swd_bus_release(SWD_BUS_OWNER_DAPLINK);
    swd_bus_release(SWD_BUS_OWNER_DAPLINK);    // second call — no-op
    TEST_ASSERT_EQUAL(SWD_BUS_OWNER_IDLE, swd_bus_owner());
}

// -----------------------------------------------------------------------------
// All consumer tags work
// -----------------------------------------------------------------------------

static void test_each_owner_can_acquire(void) {
    static const swd_bus_owner_t owners[] = {
        SWD_BUS_OWNER_CAMPAIGN,
        SWD_BUS_OWNER_SCANNER,
        SWD_BUS_OWNER_DAPLINK,
    };
    for (size_t i = 0; i < sizeof(owners)/sizeof(owners[0]); i++) {
        TEST_ASSERT_TRUE(swd_bus_try_acquire(owners[i]));
        TEST_ASSERT_EQUAL(owners[i], swd_bus_owner());
        swd_bus_release(owners[i]);
        TEST_ASSERT_EQUAL(SWD_BUS_OWNER_IDLE, swd_bus_owner());
    }
}

// -----------------------------------------------------------------------------
// Timeout shorthand
// -----------------------------------------------------------------------------

static void test_try_acquire_alias_of_timeout_zero(void) {
    // Free bus → both succeed.
    TEST_ASSERT_TRUE(swd_bus_acquire(SWD_BUS_OWNER_CAMPAIGN, SWD_BUS_TIMEOUT_NONE));
    swd_bus_release(SWD_BUS_OWNER_CAMPAIGN);
    TEST_ASSERT_TRUE(swd_bus_try_acquire(SWD_BUS_OWNER_CAMPAIGN));
    swd_bus_release(SWD_BUS_OWNER_CAMPAIGN);
}

// -----------------------------------------------------------------------------
// Runner
// -----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_idle);
    RUN_TEST(test_init_clears_held_state);

    RUN_TEST(test_acquire_when_idle_succeeds);
    RUN_TEST(test_release_returns_to_idle);
    RUN_TEST(test_acquire_after_release_succeeds);

    RUN_TEST(test_acquire_when_held_returns_false);
    RUN_TEST(test_double_acquire_same_owner_rejects);
    RUN_TEST(test_acquire_as_idle_rejects);

    RUN_TEST(test_release_when_idle_is_safe);
    RUN_TEST(test_release_wrong_owner_no_op);
    RUN_TEST(test_double_release_safe);

    RUN_TEST(test_each_owner_can_acquire);

    RUN_TEST(test_try_acquire_alias_of_timeout_zero);

    return UNITY_END();
}

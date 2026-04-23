// Unit tests for drivers/crowbar_mosfet — exercised against hal_fake.

#include "unity.h"

#include "crowbar_mosfet.h"
#include "board_v2.h"
#include "hal_fake_gpio.h"

void setUp(void) {
    hal_fake_gpio_reset();
}

void tearDown(void) {
}

// -----------------------------------------------------------------------------
// init
// -----------------------------------------------------------------------------

static void test_init_configures_both_gates_as_outputs(void) {
    crowbar_mosfet_init();
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].initialized);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].initialized);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].dir);
}

static void test_init_drives_both_gates_low(void) {
    crowbar_mosfet_init();
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
    TEST_ASSERT_EQUAL(CROWBAR_PATH_NONE, crowbar_mosfet_get_path());
}

// -----------------------------------------------------------------------------
// set_path
// -----------------------------------------------------------------------------

static void test_set_path_lp_raises_lp_only(void) {
    crowbar_mosfet_init();
    crowbar_mosfet_set_path(CROWBAR_PATH_LP);
    TEST_ASSERT_TRUE (hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
    TEST_ASSERT_EQUAL(CROWBAR_PATH_LP, crowbar_mosfet_get_path());
}

static void test_set_path_hp_raises_hp_only(void) {
    crowbar_mosfet_init();
    crowbar_mosfet_set_path(CROWBAR_PATH_HP);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_TRUE (hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
    TEST_ASSERT_EQUAL(CROWBAR_PATH_HP, crowbar_mosfet_get_path());
}

static void test_set_path_none_drops_both(void) {
    crowbar_mosfet_init();
    crowbar_mosfet_set_path(CROWBAR_PATH_LP);
    crowbar_mosfet_set_path(CROWBAR_PATH_NONE);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
    TEST_ASSERT_EQUAL(CROWBAR_PATH_NONE, crowbar_mosfet_get_path());
}

static void test_switch_lp_to_hp_break_before_make(void) {
    crowbar_mosfet_init();
    crowbar_mosfet_set_path(CROWBAR_PATH_LP);

    // Drop the previous put counts to observe this one transition.
    uint32_t lp_puts_before = hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].put_calls;
    uint32_t hp_puts_before = hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].put_calls;

    crowbar_mosfet_set_path(CROWBAR_PATH_HP);

    // During this transition the implementation must:
    //   1. write LP = false   (break — the put we care about)
    //   2. write HP = false   (part of break — sanity)
    //   3. write HP = true    (make)
    // → LP should have gotten exactly 1 new put; HP exactly 2.
    TEST_ASSERT_EQUAL_UINT32(lp_puts_before + 1u,
                             hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].put_calls);
    TEST_ASSERT_EQUAL_UINT32(hp_puts_before + 2u,
                             hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].put_calls);
    // End state must not have both HIGH.
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_TRUE (hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
}

static void test_invalid_path_treated_as_none(void) {
    crowbar_mosfet_init();
    crowbar_mosfet_set_path(CROWBAR_PATH_LP);
    crowbar_mosfet_set_path((crowbar_path_t)99);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level);
    TEST_ASSERT_EQUAL(CROWBAR_PATH_NONE, crowbar_mosfet_get_path());
}

// -----------------------------------------------------------------------------
// Invariant: at no steady-state point are both gates HIGH together.
// -----------------------------------------------------------------------------

static void test_every_sequence_end_state_has_at_most_one_high(void) {
    const crowbar_path_t sequence[] = {
        CROWBAR_PATH_LP,
        CROWBAR_PATH_HP,
        CROWBAR_PATH_LP,
        CROWBAR_PATH_NONE,
        CROWBAR_PATH_HP,
        CROWBAR_PATH_LP,
    };
    crowbar_mosfet_init();
    for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); i++) {
        crowbar_mosfet_set_path(sequence[i]);
        bool lp_high = hal_fake_gpio_states[BOARD_GP_CROWBAR_LP].level;
        bool hp_high = hal_fake_gpio_states[BOARD_GP_CROWBAR_HP].level;
        TEST_ASSERT_FALSE_MESSAGE(lp_high && hp_high,
            "Both crowbar gates are HIGH — break-before-make violated");
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_configures_both_gates_as_outputs);
    RUN_TEST(test_init_drives_both_gates_low);
    RUN_TEST(test_set_path_lp_raises_lp_only);
    RUN_TEST(test_set_path_hp_raises_hp_only);
    RUN_TEST(test_set_path_none_drops_both);
    RUN_TEST(test_switch_lp_to_hp_break_before_make);
    RUN_TEST(test_invalid_path_treated_as_none);
    RUN_TEST(test_every_sequence_end_state_has_at_most_one_high);
    return UNITY_END();
}

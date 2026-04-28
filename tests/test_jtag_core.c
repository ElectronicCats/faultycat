// Unit tests for services/jtag_core — drives jtag_* against the
// hal_fake_gpio with the F8-1 edge sampler (TCK rising edge captures
// TMS/TDI/TDO levels) and per-pin input scripts (scripted TDO bits).

#include "unity.h"

#include <string.h>

#include "board_v2.h"
#include "hal/gpio.h"
#include "hal_fake_gpio.h"
#include "jtag_core.h"

// Choose four scanner-header channels for the JTAG test pinout.
// Mapping is arbitrary as far as jtag_core is concerned; we use the
// scanner-header lower 4 (the operator would type these on the
// CDC2 shell as `jtag init 0 1 2 3`).
#define JTAG_TDI BOARD_GP_SCANNER_CH0
#define JTAG_TDO BOARD_GP_SCANNER_CH1
#define JTAG_TMS BOARD_GP_SCANNER_CH2
#define JTAG_TCK BOARD_GP_SCANNER_CH3

static jtag_pinout_t default_pins(void) {
    jtag_pinout_t p = {
        .tdi  = JTAG_TDI,
        .tdo  = JTAG_TDO,
        .tms  = JTAG_TMS,
        .tck  = JTAG_TCK,
        .trst = JTAG_PIN_TRST_NONE,
    };
    return p;
}

void setUp(void) {
    hal_fake_gpio_reset();
}

void tearDown(void) {
    if (jtag_is_inited()) jtag_deinit();
}

// -----------------------------------------------------------------------------
// Pure-function tests
// -----------------------------------------------------------------------------

static void test_bit_reverse_zero(void) {
    TEST_ASSERT_EQUAL_HEX32(0u, jtag_bit_reverse32(0u));
}

static void test_bit_reverse_all_ones(void) {
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, jtag_bit_reverse32(0xFFFFFFFFu));
}

static void test_bit_reverse_lsb_to_msb(void) {
    TEST_ASSERT_EQUAL_HEX32(0x80000000u, jtag_bit_reverse32(0x00000001u));
    TEST_ASSERT_EQUAL_HEX32(0x00000001u, jtag_bit_reverse32(0x80000000u));
}

static void test_bit_reverse_known_idcode(void) {
    // STM32F103 IDCODE 0x1BA01477 reversed = 0xEE2805D8.
    TEST_ASSERT_EQUAL_HEX32(0xEE2805D8u, jtag_bit_reverse32(0x1BA01477u));
    TEST_ASSERT_EQUAL_HEX32(0x1BA01477u, jtag_bit_reverse32(0xEE2805D8u));
}

static void test_idcode_valid_known_targets(void) {
    // STM32F103: mfg=ARM (id=0x3B, bank=4) part=0xBA01 ver=0x1 — bit0=1.
    TEST_ASSERT_TRUE (jtag_idcode_is_valid(0x1BA01477u));
    TEST_ASSERT_TRUE (jtag_idcode_is_valid(0x4BA00477u));   // hypothetical Cortex
}

static void test_idcode_invalid_sentinels(void) {
    TEST_ASSERT_FALSE(jtag_idcode_is_valid(0x00000000u));
    TEST_ASSERT_FALSE(jtag_idcode_is_valid(0xFFFFFFFFu));
}

static void test_idcode_invalid_lsb_zero(void) {
    // bit 0 must be 1 by IEEE 1149.1.
    TEST_ASSERT_FALSE(jtag_idcode_is_valid(0x1BA01476u));
}

static void test_idcode_invalid_mfg_zero(void) {
    // mfg id == 0 → invalid.
    TEST_ASSERT_FALSE(jtag_idcode_is_valid(0x12340001u));
}

static void test_permutations_count(void) {
    // P(N,4) for the standard scanner widths.
    TEST_ASSERT_EQUAL_UINT32(0u,    jtag_permutations_count(0));
    TEST_ASSERT_EQUAL_UINT32(0u,    jtag_permutations_count(3));
    TEST_ASSERT_EQUAL_UINT32(24u,   jtag_permutations_count(4));    // 4!
    TEST_ASSERT_EQUAL_UINT32(1680u, jtag_permutations_count(8));    // FaultyCat v2.x
    TEST_ASSERT_EQUAL_UINT32(43680u, jtag_permutations_count(16));  // blueTag standalone
}

// -----------------------------------------------------------------------------
// Init / deinit / pin validation
// -----------------------------------------------------------------------------

static void test_init_rejects_null(void) {
    TEST_ASSERT_FALSE(jtag_init(NULL));
    TEST_ASSERT_FALSE(jtag_is_inited());
}

static void test_init_rejects_pin_collisions(void) {
    jtag_pinout_t p = default_pins();
    p.tdi = p.tdo;
    TEST_ASSERT_FALSE(jtag_init(&p));

    p = default_pins();
    p.tms = p.tck;
    TEST_ASSERT_FALSE(jtag_init(&p));

    p = default_pins();
    p.trst = (int8_t)p.tdi;
    TEST_ASSERT_FALSE(jtag_init(&p));
}

static void test_init_rejects_out_of_range(void) {
    jtag_pinout_t p = default_pins();
    p.tdi = 31;   // GP31 doesn't exist on RP2040
    TEST_ASSERT_FALSE(jtag_init(&p));
}

static void test_init_blocks_double_init(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE (jtag_init(&p));
    TEST_ASSERT_TRUE (jtag_is_inited());
    TEST_ASSERT_FALSE(jtag_init(&p));   // already inited → reject
}

static void test_init_configures_directions(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[JTAG_TDI].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[JTAG_TMS].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_OUT, hal_fake_gpio_states[JTAG_TCK].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN,  hal_fake_gpio_states[JTAG_TDO].dir);
    // TDO must have pull-up so an unconnected target floats high.
    TEST_ASSERT_TRUE (hal_fake_gpio_states[JTAG_TDO].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[JTAG_TDO].pull_down);
    // TCK must idle low.
    TEST_ASSERT_FALSE(hal_fake_gpio_states[JTAG_TCK].level);
}

static void test_deinit_releases_pins(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    jtag_deinit();
    TEST_ASSERT_FALSE(jtag_is_inited());
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[JTAG_TDI].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[JTAG_TMS].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[JTAG_TCK].dir);
    TEST_ASSERT_EQUAL(HAL_GPIO_DIR_IN, hal_fake_gpio_states[JTAG_TDO].dir);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[JTAG_TDO].pull_up);
}

static void test_deinit_idempotent(void) {
    jtag_deinit();   // not inited yet — must be safe
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    jtag_deinit();
    jtag_deinit();   // double deinit — must be safe
    TEST_ASSERT_FALSE(jtag_is_inited());
}

// -----------------------------------------------------------------------------
// TAP transitions — verified via TCK-rising-edge sampler that captures
// TMS + TDI levels at every rising edge.
// -----------------------------------------------------------------------------

static void configure_jtag_edge_sampler(void) {
    // watch[0] = TMS, watch[1] = TDI; TDO unused (consumed via input
    // script so the level field doesn't reflect what was sampled).
    hal_fake_gpio_edge_sampler_configure(JTAG_TCK, JTAG_TMS, JTAG_TDI,
                                         HAL_FAKE_GPIO_PIN_NONE,
                                         HAL_FAKE_GPIO_PIN_NONE);
}

static void test_reset_to_rti_drives_5_high_then_1_low(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    configure_jtag_edge_sampler();
    hal_fake_gpio_edge_sampler_reset();   // start fresh past init's
                                          // (zero) edges

    jtag_reset_to_run_test_idle();

    TEST_ASSERT_EQUAL_size_t(6u, hal_fake_gpio_edge_sampler_count());
    for (size_t i = 0; i < 5; i++) {
        hal_fake_gpio_edge_sample_t e = hal_fake_gpio_edge_sampler_at(i);
        TEST_ASSERT_TRUE_MESSAGE(e.watch[0],
            "First 5 TCK edges must be sampled with TMS=1 (→ TLR)");
    }
    hal_fake_gpio_edge_sample_t e6 = hal_fake_gpio_edge_sampler_at(5);
    TEST_ASSERT_FALSE_MESSAGE(e6.watch[0],
        "6th TCK edge must be sampled with TMS=0 (→ Run-Test/Idle)");
}

// detect_chain_length pre-loop drives 1024 + 5 + 32 = 1061 TMS values
// before the read loop. Hard to assert each one — but we CAN verify
// the high-level structure: after the IR-fill (1024 TMS=0), there's
// the manual walk Shift-IR → Exit1 → Update → Select-DR → Capture →
// Shift-DR encoded as TMS=[1,1,1,0,0]. We sample a window around
// that boundary.
static void test_detect_chain_no_target_returns_zero(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    // No input script loaded → hal_gpio_get(TDO) returns the pin's
    // `level`, which is 0 (zeroed by reset). detect_chain_length's
    // detect-loop reads false on iteration 0 → returns 1, NOT 0.
    // To simulate "no target" we instead pull TDO HIGH (which is
    // what a floating bus with pull-up does in real life). Set the
    // level field directly — that's what the live HW would have
    // post-pull-up.
    hal_fake_gpio_states[JTAG_TDO].level = true;

    size_t n = jtag_detect_chain_length();
    TEST_ASSERT_EQUAL_size_t(0u, n);
}

static void test_detect_chain_one_device(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    // Script TDO = 1, 1, 1, ..., 0 (the 0 lands at iteration 0 of the
    // detect-loop because we drove TDI=1 prior, then a single TDI=0
    // shifts through 1 BYPASS register and appears at TDO at clock 1).
    bool tdo_script[64];
    for (size_t i = 0; i < 64; i++) tdo_script[i] = (i != 0);
    hal_fake_gpio_input_script_load(JTAG_TDO, tdo_script, 64);

    size_t n = jtag_detect_chain_length();
    TEST_ASSERT_EQUAL_size_t(1u, n);
}

static void test_detect_chain_three_devices(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    // 0 appears at clock 3 → chain length 3.
    bool tdo_script[64];
    for (size_t i = 0; i < 64; i++) tdo_script[i] = (i != 2);
    hal_fake_gpio_input_script_load(JTAG_TDO, tdo_script, 64);

    size_t n = jtag_detect_chain_length();
    TEST_ASSERT_EQUAL_size_t(3u, n);
}

// -----------------------------------------------------------------------------
// IDCODE readout — single device
// -----------------------------------------------------------------------------

// Build a full TDO script for jtag_read_idcodes against a 1-device
// chain that returns `idcode`. Layout: [chain-detect: 0 at clock 1]
// followed by [32 IDCODE bits, LSB-first on the wire].
static void load_one_device_idcode_script(uint32_t idcode) {
    bool script[1 + 32];
    script[0] = false;                         // detect-loop iter 0 sees 0 → chain=1
    for (uint8_t b = 0; b < 32; b++) {
        script[1 + b] = (bool)((idcode >> b) & 1u);
    }
    hal_fake_gpio_input_script_load(JTAG_TDO, script, sizeof(script));
}

static void test_read_idcodes_single_device_stm32(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    load_one_device_idcode_script(0x1BA01477u);

    uint32_t out[4] = {0};
    size_t n = jtag_read_idcodes(out, 4);
    TEST_ASSERT_EQUAL_size_t(1u, n);
    TEST_ASSERT_EQUAL_HEX32(0x1BA01477u, out[0]);
    TEST_ASSERT_TRUE(jtag_idcode_is_valid(out[0]));
}

static void test_read_idcodes_no_target_returns_zero(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    // Floating bus (pull-up wins) → detect_chain_length=0 → read returns 0.
    hal_fake_gpio_states[JTAG_TDO].level = true;

    uint32_t out[4] = {0xDEADBEEFu, 0u, 0u, 0u};
    size_t n = jtag_read_idcodes(out, 4);
    TEST_ASSERT_EQUAL_size_t(0u, n);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, out[0]);   // out untouched
}

static void test_read_idcodes_rejects_uninit(void) {
    // Don't call jtag_init.
    uint32_t out[4];
    TEST_ASSERT_EQUAL_size_t(0u, jtag_read_idcodes(out, 4));
}

static void test_read_idcodes_rejects_zero_max(void) {
    jtag_pinout_t p = default_pins();
    TEST_ASSERT_TRUE(jtag_init(&p));
    uint32_t out[1];
    TEST_ASSERT_EQUAL_size_t(0u, jtag_read_idcodes(out, 0));
}

// -----------------------------------------------------------------------------
// Runner
// -----------------------------------------------------------------------------

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_bit_reverse_zero);
    RUN_TEST(test_bit_reverse_all_ones);
    RUN_TEST(test_bit_reverse_lsb_to_msb);
    RUN_TEST(test_bit_reverse_known_idcode);

    RUN_TEST(test_idcode_valid_known_targets);
    RUN_TEST(test_idcode_invalid_sentinels);
    RUN_TEST(test_idcode_invalid_lsb_zero);
    RUN_TEST(test_idcode_invalid_mfg_zero);

    RUN_TEST(test_permutations_count);

    RUN_TEST(test_init_rejects_null);
    RUN_TEST(test_init_rejects_pin_collisions);
    RUN_TEST(test_init_rejects_out_of_range);
    RUN_TEST(test_init_blocks_double_init);
    RUN_TEST(test_init_configures_directions);
    RUN_TEST(test_deinit_releases_pins);
    RUN_TEST(test_deinit_idempotent);

    RUN_TEST(test_reset_to_rti_drives_5_high_then_1_low);
    RUN_TEST(test_detect_chain_no_target_returns_zero);
    RUN_TEST(test_detect_chain_one_device);
    RUN_TEST(test_detect_chain_three_devices);

    RUN_TEST(test_read_idcodes_single_device_stm32);
    RUN_TEST(test_read_idcodes_no_target_returns_zero);
    RUN_TEST(test_read_idcodes_rejects_uninit);
    RUN_TEST(test_read_idcodes_rejects_zero_max);

    return UNITY_END();
}

// Unit tests for services/swd_core/swd_phy — exercised against the
// hal_fake_pio + hal_fake_gpio fakes. Validates the PIO program
// shape, FIFO command encoding, and the runtime-pin contract.

#include "unity.h"

#include <string.h>

#include "board_v2.h"
#include "hal/pio.h"
#include "hal_fake_gpio.h"
#include "hal_fake_pio.h"
#include "swd_phy.h"

static const uint8_t SWCLK = BOARD_GP_SCANNER_CH0;   // GP0 default
static const uint8_t SWDIO = BOARD_GP_SCANNER_CH1;   // GP1 default
static const uint8_t NRST  = BOARD_GP_SCANNER_CH2;   // GP2 default

void setUp(void) {
    hal_fake_pio_reset();
    hal_fake_gpio_reset();
}

void tearDown(void) {
    swd_phy_deinit();
}

// -----------------------------------------------------------------------------
// init / deinit
// -----------------------------------------------------------------------------

static void test_init_claims_pio1_sm0(void) {
    TEST_ASSERT_TRUE(swd_phy_init(SWCLK, SWDIO, NRST));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[1].sm[0].claimed);
    // pio0 (glitch engines) untouched.
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[0].claimed);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[0].sm[1].claimed);
}

static void test_init_rejects_duplicate(void) {
    TEST_ASSERT_TRUE(swd_phy_init(SWCLK, SWDIO, NRST));
    TEST_ASSERT_FALSE(swd_phy_init(SWCLK, SWDIO, NRST));
}

static void test_init_rejects_swclk_equal_swdio(void) {
    TEST_ASSERT_FALSE(swd_phy_init(SWCLK, SWCLK, NRST));
}

static void test_init_rejects_out_of_range_pin(void) {
    TEST_ASSERT_FALSE(swd_phy_init(99u, SWDIO, NRST));
    TEST_ASSERT_FALSE(swd_phy_init(SWCLK, 99u, NRST));
}

static void test_init_loads_11_instruction_program(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    TEST_ASSERT_EQUAL_UINT32(11u, hal_fake_pio_insts[1].program.length);
}

static void test_init_program_opcode_signature(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    // Spot-check the first / wrap_target / read_cmd / push opcodes.
    // If pioasm-vs-handcode drifts, these change first.
    TEST_ASSERT_EQUAL_HEX16(0x80A0, hal_fake_pio_insts[1].program.instructions[0]);
    TEST_ASSERT_EQUAL_HEX16(0x90A0, hal_fake_pio_insts[1].program.instructions[3]);
    TEST_ASSERT_EQUAL_HEX16(0x60A5, hal_fake_pio_insts[1].program.instructions[6]);
    TEST_ASSERT_EQUAL_HEX16(0x5901, hal_fake_pio_insts[1].program.instructions[8]);
    TEST_ASSERT_EQUAL_HEX16(0x8020, hal_fake_pio_insts[1].program.instructions[10]);
}

static void test_init_configures_wrap_and_shift_right(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    // wrap fields are stored RELATIVE in the cfg per the new HAL
    // contract (F6-2 extension). The rp2040 backend adds the load
    // offset; the fake just records the raw cfg.
    TEST_ASSERT_EQUAL_UINT32(3u,  sm->last_cfg.wrap_target);
    TEST_ASSERT_EQUAL_UINT32(10u, sm->last_cfg.wrap_end);
    TEST_ASSERT_TRUE(sm->last_cfg.out_shift_right);
    TEST_ASSERT_TRUE(sm->last_cfg.in_shift_right);
}

static void test_init_binds_pins_correctly(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    TEST_ASSERT_EQUAL_UINT32(SWDIO, sm->last_cfg.set_pin_base);
    TEST_ASSERT_EQUAL_UINT32(SWDIO, sm->last_cfg.out_pin_base);
    TEST_ASSERT_EQUAL_UINT32(SWDIO, sm->last_cfg.in_pin_base);
    TEST_ASSERT_EQUAL_UINT32(SWCLK, sm->last_cfg.sideset_pin_base);
    TEST_ASSERT_EQUAL_UINT32(1u, sm->last_cfg.set_pin_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sm->last_cfg.out_pin_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sm->last_cfg.in_pin_count);
    TEST_ASSERT_EQUAL_UINT32(1u, sm->last_cfg.sideset_pin_count);
    TEST_ASSERT_TRUE(sm->last_cfg.sideset_optional);
    // GPIO_init was called for both pins through pio_gpio_init.
    TEST_ASSERT_TRUE(hal_fake_pio_insts[1].gpio_init_bitmap & (1u << SWCLK));
    TEST_ASSERT_TRUE(hal_fake_pio_insts[1].gpio_init_bitmap & (1u << SWDIO));
}

static void test_init_bootstraps_with_jmp_to_dispatcher(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    // Init now runs two execs: SET PINS,0 (preset SWDIO output to
    // 0 for the open-drain emulation) followed by JMP to dispatcher.
    TEST_ASSERT_EQUAL_UINT32(2u, sm->exec_calls);
    // Last exec is the JMP to GET_NEXT_CMD (offset 3, no sideset/delay).
    TEST_ASSERT_EQUAL_HEX16(0x0003, sm->last_exec_instr);
    // SM is enabled after bootstrap so the dispatcher actually runs.
    TEST_ASSERT_TRUE(sm->enabled);
}

static void test_init_with_nrst_pulls_up(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    TEST_ASSERT_TRUE(hal_fake_gpio_states[NRST].pull_up);
    TEST_ASSERT_FALSE(hal_fake_gpio_states[NRST].pull_down);
    TEST_ASSERT_FALSE((hal_fake_gpio_states[NRST].dir == HAL_GPIO_DIR_OUT));
}

static void test_init_without_nrst_leaves_no_pin_touched(void) {
    // Pre-state: NRST pin idle.
    TEST_ASSERT_FALSE(hal_fake_gpio_states[NRST].pull_up);
    swd_phy_init(SWCLK, SWDIO, SWD_PHY_NRST_NONE);
    // No nrst → reset level returns -1.
    TEST_ASSERT_EQUAL_INT(-1, swd_phy_reset_level());
    // assert_reset is a no-op without nrst.
    swd_phy_assert_reset(true);
    TEST_ASSERT_FALSE((hal_fake_gpio_states[NRST].dir == HAL_GPIO_DIR_OUT));
}

static void test_deinit_unclaims_and_restores_pins(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    swd_phy_deinit();
    TEST_ASSERT_FALSE(hal_fake_pio_insts[1].sm[0].claimed);
    TEST_ASSERT_FALSE(hal_fake_pio_insts[1].program.loaded);
    // SWDIO pull released so scanner_io can re-claim cleanly.
    TEST_ASSERT_FALSE(hal_fake_gpio_states[SWDIO].pull_up);
    TEST_ASSERT_FALSE((hal_fake_gpio_states[SWDIO].dir == HAL_GPIO_DIR_OUT));
}

// -----------------------------------------------------------------------------
// set_clk_khz
// -----------------------------------------------------------------------------

static void test_set_clk_khz_default_divider(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    // Default 1 MHz: divider = ceil(125000/1000)/4 = 125/4 → 32 (round up).
    // The init float clk_div is what configure() saw; the integer
    // divider path is exercised by an explicit set_clk_khz call.
    swd_phy_set_clk_khz(1000u);
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    TEST_ASSERT_EQUAL_UINT32(1u, sm->set_clkdiv_int_calls);
    // ceil(125000/1000)=125; +3 then /4 = 32.
    TEST_ASSERT_EQUAL_UINT32(32u, sm->last_clkdiv_int);
}

static void test_set_clk_khz_clamps_to_min(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    swd_phy_set_clk_khz(10u);   // below SWD_PHY_CLK_MIN_KHZ (100)
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    // ceil(125000/100)=1250; +3 then /4 = 313.
    TEST_ASSERT_EQUAL_UINT32(313u, sm->last_clkdiv_int);
}

static void test_set_clk_khz_clamps_to_max(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    swd_phy_set_clk_khz(100000u);   // above SWD_PHY_CLK_MAX_KHZ (24 MHz)
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    // ceil(125000/24000)=6; +3 then /4 = 2.
    TEST_ASSERT_EQUAL_UINT32(2u, sm->last_clkdiv_int);
}

static void test_set_clk_khz_no_op_without_init(void) {
    swd_phy_set_clk_khz(1000u);
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_pio_insts[1].sm[0].set_clkdiv_int_calls);
}

// -----------------------------------------------------------------------------
// write_bits / read_bits / mode switches
// -----------------------------------------------------------------------------

// FIFO command word layout (host side):
//   bits 13..9 : cmd_addr (= offset + label)
//   bit  8     : SWDIO out enable
//   bits 7..0  : bit_count - 1
// Helper that decodes the count and dir for tests so the assertions
// don't repeat the bit math.
static uint32_t cmd_count(uint32_t word) { return (word & 0xFFu) + 1u; }
static bool     cmd_dir  (uint32_t word) { return (word >> 8) & 1u; }
static uint32_t cmd_pc   (uint32_t word) { return (word >> 9) & 0x1Fu; }

static void test_write_bits_emits_cmd_then_data(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_insts[1].sm[0].tx_count = 0;   // ignore bootstrap entries
    swd_phy_write_bits(8u, 0x55u);
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    TEST_ASSERT_EQUAL_UINT32(2u, sm->tx_count);
    TEST_ASSERT_EQUAL_UINT32(8u, cmd_count(sm->tx_fifo[0]));
    TEST_ASSERT_TRUE(cmd_dir(sm->tx_fifo[0]));
    TEST_ASSERT_EQUAL_UINT32(0u, cmd_pc(sm->tx_fifo[0]));   // write_cmd at 0
    // Open-drain emulation: data is XOR-inverted before push so the
    // PIO bitloop's `out pindirs, 1` produces the wire pattern the
    // caller intended (push-pull semantics).
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFAAu, sm->tx_fifo[1]);   // ~0x55
}

static void test_write_bits_rejects_zero_and_overflow(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_insts[1].sm[0].tx_count = 0;
    swd_phy_write_bits(0u, 0xAAu);
    swd_phy_write_bits(33u, 0xAAu);
    TEST_ASSERT_EQUAL_UINT32(0u, hal_fake_pio_insts[1].sm[0].tx_count);
}

static void test_read_bits_emits_cmd_and_shifts_result(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_insts[1].sm[0].tx_count = 0;
    // The fake returns the next RX word. Push a 32-bit value that
    // would correspond to 8 MSB-aligned data bits = 0xAB000000;
    // shift right by (32 - 8) = 24 → 0xAB.
    hal_fake_pio_push_rx(1, 0, 0xAB000000u);
    uint32_t got = swd_phy_read_bits(8u);
    TEST_ASSERT_EQUAL_HEX32(0xABu, got);
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    TEST_ASSERT_EQUAL_UINT32(1u, sm->tx_count);
    TEST_ASSERT_EQUAL_UINT32(8u, cmd_count(sm->tx_fifo[0]));
    TEST_ASSERT_FALSE(cmd_dir(sm->tx_fifo[0]));
    TEST_ASSERT_EQUAL_UINT32(8u, cmd_pc(sm->tx_fifo[0]));   // read_cmd at 8
}

static void test_read_mode_sends_skip_with_dir_off(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_insts[1].sm[0].tx_count = 0;
    swd_phy_read_mode();
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    TEST_ASSERT_EQUAL_UINT32(1u, sm->tx_count);
    TEST_ASSERT_EQUAL_UINT32(1u, cmd_count(sm->tx_fifo[0]));
    TEST_ASSERT_FALSE(cmd_dir(sm->tx_fifo[0]));
    TEST_ASSERT_EQUAL_UINT32(3u, cmd_pc(sm->tx_fifo[0]));   // get_next_cmd at 3
}

static void test_write_mode_sends_skip_with_dir_on(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_insts[1].sm[0].tx_count = 0;
    swd_phy_write_mode();
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    TEST_ASSERT_EQUAL_UINT32(1u, sm->tx_count);
    TEST_ASSERT_TRUE(cmd_dir(sm->tx_fifo[0]));
    TEST_ASSERT_EQUAL_UINT32(3u, cmd_pc(sm->tx_fifo[0]));
}

static void test_hiz_clocks_emits_turnaround_zero_data(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    hal_fake_pio_insts[1].sm[0].tx_count = 0;
    swd_phy_hiz_clocks(8u);
    hal_fake_pio_sm_state_t *sm = &hal_fake_pio_insts[1].sm[0];
    TEST_ASSERT_EQUAL_UINT32(2u, sm->tx_count);
    TEST_ASSERT_FALSE(cmd_dir(sm->tx_fifo[0]));
    TEST_ASSERT_EQUAL_UINT32(0u, cmd_pc(sm->tx_fifo[0]));   // turnaround_cmd at 0
    TEST_ASSERT_EQUAL_UINT32(0u, sm->tx_fifo[1]);            // dummy data
}

// -----------------------------------------------------------------------------
// nRST
// -----------------------------------------------------------------------------

static void test_assert_reset_drives_low(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    swd_phy_assert_reset(true);
    TEST_ASSERT_TRUE((hal_fake_gpio_states[NRST].dir == HAL_GPIO_DIR_OUT));
    TEST_ASSERT_FALSE(hal_fake_gpio_states[NRST].level);
}

static void test_release_reset_returns_to_pullup(void) {
    swd_phy_init(SWCLK, SWDIO, NRST);
    swd_phy_assert_reset(true);
    swd_phy_assert_reset(false);
    TEST_ASSERT_FALSE((hal_fake_gpio_states[NRST].dir == HAL_GPIO_DIR_OUT));
    TEST_ASSERT_TRUE(hal_fake_gpio_states[NRST].pull_up);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_init_claims_pio1_sm0);
    RUN_TEST(test_init_rejects_duplicate);
    RUN_TEST(test_init_rejects_swclk_equal_swdio);
    RUN_TEST(test_init_rejects_out_of_range_pin);
    RUN_TEST(test_init_loads_11_instruction_program);
    RUN_TEST(test_init_program_opcode_signature);
    RUN_TEST(test_init_configures_wrap_and_shift_right);
    RUN_TEST(test_init_binds_pins_correctly);
    RUN_TEST(test_init_bootstraps_with_jmp_to_dispatcher);
    RUN_TEST(test_init_with_nrst_pulls_up);
    RUN_TEST(test_init_without_nrst_leaves_no_pin_touched);
    RUN_TEST(test_deinit_unclaims_and_restores_pins);
    RUN_TEST(test_set_clk_khz_default_divider);
    RUN_TEST(test_set_clk_khz_clamps_to_min);
    RUN_TEST(test_set_clk_khz_clamps_to_max);
    RUN_TEST(test_set_clk_khz_no_op_without_init);
    RUN_TEST(test_write_bits_emits_cmd_then_data);
    RUN_TEST(test_write_bits_rejects_zero_and_overflow);
    RUN_TEST(test_read_bits_emits_cmd_and_shifts_result);
    RUN_TEST(test_read_mode_sends_skip_with_dir_off);
    RUN_TEST(test_write_mode_sends_skip_with_dir_on);
    RUN_TEST(test_hiz_clocks_emits_turnaround_zero_data);
    RUN_TEST(test_assert_reset_drives_low);
    RUN_TEST(test_release_reset_returns_to_pullup);
    return UNITY_END();
}

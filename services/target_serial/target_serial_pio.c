#include "target_serial_pio.h"

#include "hal/gpio.h"
#include "hal/pio.h"

// PIO UART programs derived from raspberrypi/pico-examples (BSD-3):
//   pio/uart_tx/uart_tx.pio  -> s_tx_prog (verbatim, 8 cycles/bit)
//   pio/uart_rx/uart_rx.pio  -> s_rx_prog (uart_rx_mini + explicit
//                               push, since hal_pio_sm_cfg_t exposes
//                               no autopush threshold)
// Both run the SM at 8x baud. Hand-encoded uint16_t (no pioasm step in
// this tree); jmp targets are 0-based within each program and relocated
// by hal_pio_add_program (== pico-sdk pio_add_program).

#define TS_TX_PROG_LEN 4u
static const uint16_t s_tx_prog[TS_TX_PROG_LEN] = {
    0x9fa0u, // 0: pull   block        side 1 [7]
    0xf727u, // 1: set    x, 7         side 0 [7]
    0x6001u, // 2: out    pins, 1                 (bitloop)
    0x0642u, // 3: jmp    x--, 2               [6]
};

#define TS_RX_PROG_LEN 6u
static const uint16_t s_rx_prog[TS_RX_PROG_LEN] = {
    0x2020u, // 0: wait   0 pin, 0
    0xea27u, // 1: set    x, 7                [10]
    0x4001u, // 2: in     pins, 1                 (bitloop)
    0x0642u, // 3: jmp    x--, 2               [6]
    0x8020u, // 4: push   block
    0x0000u, // 5: jmp    0
};

#define TS_TX_SM 1u // pio1/SM1 (SM0 is swd_phy)
#define TS_RX_SM 2u // pio1/SM2

static hal_pio_inst_t* s_pio = NULL;
static uint32_t s_tx_off     = 0u;
static uint32_t s_rx_off     = 0u;
static uint8_t s_tx_gp       = 0u;
static uint8_t s_rx_gp       = 0u;
static bool s_inited         = false;

bool target_serial_pio_init(uint8_t tx_gp, uint8_t rx_gp, uint32_t divider) {
    if (s_inited)
        return false;
    s_pio = hal_pio_instance(1); // pio1
    if (!s_pio)
        return false;
    if (!hal_pio_claim_sm(s_pio, TS_TX_SM))
        return false;
    if (!hal_pio_claim_sm(s_pio, TS_RX_SM)) {
        hal_pio_unclaim_sm(s_pio, TS_TX_SM);
        return false;
    }

    hal_pio_program_t txp = {.instructions = s_tx_prog, .length = TS_TX_PROG_LEN, .origin = -1};
    if (!hal_pio_add_program(s_pio, &txp, &s_tx_off)) {
        hal_pio_unclaim_sm(s_pio, TS_RX_SM);
        hal_pio_unclaim_sm(s_pio, TS_TX_SM);
        return false;
    }
    hal_pio_program_t rxp = {.instructions = s_rx_prog, .length = TS_RX_PROG_LEN, .origin = -1};
    if (!hal_pio_add_program(s_pio, &rxp, &s_rx_off)) {
        hal_pio_remove_program(s_pio, &txp, s_tx_off);
        hal_pio_unclaim_sm(s_pio, TS_RX_SM);
        hal_pio_unclaim_sm(s_pio, TS_TX_SM);
        return false;
    }

    // TX pin: PIO-owned output; preset HIGH below so idle = UART mark.
    hal_pio_gpio_init(s_pio, tx_gp);
    hal_pio_set_consecutive_pindirs(s_pio, TS_TX_SM, tx_gp, 1, true);
    // RX pin: PIO-owned input, pulled HIGH (UART idle).
    hal_pio_gpio_init(s_pio, rx_gp);
    hal_pio_set_consecutive_pindirs(s_pio, TS_RX_SM, rx_gp, 1, false);
    hal_gpio_set_pulls((hal_gpio_pin_t)rx_gp, true, false);

    hal_pio_sm_cfg_t txc = {
        .set_pin_base      = tx_gp,
        .set_pin_count     = 1,
        .out_pin_base      = tx_gp,
        .out_pin_count     = 1,
        .sideset_pin_base  = tx_gp,
        .sideset_pin_count = 1,
        .sideset_optional  = true,
        .sideset_pindirs   = false,
        .out_shift_right   = true, // UART is LSB-first
        .clk_div           = (float)divider,
    };
    hal_pio_sm_configure(s_pio, TS_TX_SM, s_tx_off, &txc);
    hal_pio_sm_clear_fifos(s_pio, TS_TX_SM);
    // Preset TX line HIGH (SET PINS,1 = 0xE001) so the line idles at a
    // mark instead of driving a spurious break before the first byte.
    hal_pio_sm_exec(s_pio, TS_TX_SM, 0xE001u);
    hal_pio_sm_set_enabled(s_pio, TS_TX_SM, true);

    hal_pio_sm_cfg_t rxc = {
        .in_pin_base    = rx_gp,
        .in_pin_count   = 1,
        .in_shift_right = true, // LSB-first; byte lands in word[31:24]
        .clk_div        = (float)divider,
    };
    hal_pio_sm_configure(s_pio, TS_RX_SM, s_rx_off, &rxc);
    hal_pio_sm_clear_fifos(s_pio, TS_RX_SM);
    hal_pio_sm_set_enabled(s_pio, TS_RX_SM, true);

    s_tx_gp  = tx_gp;
    s_rx_gp  = rx_gp;
    s_inited = true;
    return true;
}

void target_serial_pio_deinit(void) {
    if (!s_inited)
        return;
    hal_pio_sm_set_enabled(s_pio, TS_TX_SM, false);
    hal_pio_sm_set_enabled(s_pio, TS_RX_SM, false);

    hal_pio_program_t txp = {.instructions = s_tx_prog, .length = TS_TX_PROG_LEN, .origin = -1};
    hal_pio_program_t rxp = {.instructions = s_rx_prog, .length = TS_RX_PROG_LEN, .origin = -1};
    hal_pio_remove_program(s_pio, &rxp, s_rx_off);
    hal_pio_remove_program(s_pio, &txp, s_tx_off);
    hal_pio_unclaim_sm(s_pio, TS_RX_SM);
    hal_pio_unclaim_sm(s_pio, TS_TX_SM);

    // Restore pins so scanner_io / swd_phy can re-claim them.
    hal_gpio_init((hal_gpio_pin_t)s_tx_gp, HAL_GPIO_DIR_IN);
    hal_gpio_init((hal_gpio_pin_t)s_rx_gp, HAL_GPIO_DIR_IN);
    hal_gpio_set_pulls((hal_gpio_pin_t)s_rx_gp, false, false);

    s_pio    = NULL;
    s_inited = false;
}

void target_serial_pio_set_divider(uint32_t divider) {
    if (!s_inited)
        return;
    hal_pio_sm_set_clkdiv_int(s_pio, TS_TX_SM, divider);
    hal_pio_sm_set_clkdiv_int(s_pio, TS_RX_SM, divider);
}

bool target_serial_pio_try_put(uint8_t b) {
    if (!s_inited)
        return false;
    return hal_pio_sm_try_put(s_pio, TS_TX_SM, (uint32_t)b);
}

bool target_serial_pio_try_get(uint8_t* out) {
    if (!s_inited || out == NULL)
        return false;
    uint32_t w = 0u;
    if (!hal_pio_sm_try_get(s_pio, TS_RX_SM, &w))
        return false;
    *out = (uint8_t)(w >> 24); // RX shifts right -> byte in high 8 bits
    return true;
}

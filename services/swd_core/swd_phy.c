#include "swd_phy.h"

// F6-1 skeleton — API-only stub. F6-2 lands the real port from
// third_party/debugprobe/src/probe.{c,pio} (MIT) with runtime pins.

bool swd_phy_init(uint8_t swclk_gp, uint8_t swdio_gp, int8_t nrst_gp) {
    (void)swclk_gp; (void)swdio_gp; (void)nrst_gp;
    return false;
}

void swd_phy_deinit(void) {
}

void swd_phy_set_clk_khz(uint32_t khz) {
    (void)khz;
}

void swd_phy_write_bits(uint32_t bit_count, uint32_t data) {
    (void)bit_count; (void)data;
}

uint32_t swd_phy_read_bits(uint32_t bit_count) {
    (void)bit_count;
    return 0u;
}

void swd_phy_hiz_clocks(uint32_t bit_count) {
    (void)bit_count;
}

void swd_phy_read_mode(void) {
}

void swd_phy_write_mode(void) {
}

void swd_phy_assert_reset(bool asserted) {
    (void)asserted;
}

int swd_phy_reset_level(void) {
    return -1;
}

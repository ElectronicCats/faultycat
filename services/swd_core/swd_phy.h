#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/swd_core/swd_phy — SWD physical layer (PIO bit-bang).
//
// Ported from raspberrypi/debugprobe @ v2.3.0 (MIT) — variant
// PROBE_IO_RAW (no level-shifter; SWDIO bidirectional on a single
// MCU pin via PIO set pindirs). Upstream layout:
//   third_party/debugprobe/src/probe.c     → swd_phy.c
//   third_party/debugprobe/src/probe.pio   → swd.pio
//
// FaultyCat-specific differences vs upstream:
//   1. SWCLK / SWDIO / nRST pins are RUNTIME parameters (not
//      compile-time PROBE_PIN_OFFSET). v2.x routes SWD over the
//      scanner header (Conn_01x10, GP0..GP7); the operator picks
//      which two channels are SWCLK/SWDIO at session start. blueTag
//      (F8) will auto-detect.
//   2. PIO instance is pio1/SM0 (pio0 is saturated by glitch
//      engines — EMFI on SM0/IRQ0, crowbar on SM1/IRQ1, frozen at
//      F4-1 / F5-2). pio1 begins its use here in F6.
//   3. No FreeRTOS dependency. The probe_info / probe_debug /
//      probe_dump macros that wrap printf in vTaskSuspendAll were
//      replaced with no-ops; diagnostic output (when wanted) goes
//      through main.c::diag_printf into the CDC2 scanner shell.
//   4. swd_phy and drivers/scanner_io share GP0..GP7 — only one
//      may own a given pin at a time. F6 documents the contract;
//      F9 lands the formal mutex with daplink_usb / pinout_scanner.

#define SWD_PHY_CLK_DEFAULT_KHZ   1000u   // 1 MHz — safe default
#define SWD_PHY_CLK_MIN_KHZ       100u
#define SWD_PHY_CLK_MAX_KHZ       24000u

#define SWD_PHY_NRST_NONE         (-1)    // pass to init when no reset wired

// Claim pio1/SM0, attach the SWD program, configure the chosen pins.
// `swclk_gp` and `swdio_gp` are GPIO numbers (typically two channels
// of the scanner header, defaults BOARD_GP_SCANNER_CH0/CH1).
// `nrst_gp` is either a GPIO number or SWD_PHY_NRST_NONE if no
// target reset line is wired. Returns false if the SM is already
// claimed elsewhere or if the pin numbers are out of range.
bool swd_phy_init(uint8_t swclk_gp, uint8_t swdio_gp, int8_t nrst_gp);

// Tear down: stop SM, release the program, restore the chosen pins
// to plain GPIO inputs (so scanner_io can re-claim them). Safe to
// call repeatedly.
void swd_phy_deinit(void);

// Re-program the PIO clock divisor for the requested SWCLK rate.
// Range SWD_PHY_CLK_MIN_KHZ..SWD_PHY_CLK_MAX_KHZ (clamped). No-op
// if init() was not called.
void swd_phy_set_clk_khz(uint32_t khz);

// Bit shifters — `bit_count` in 1..32. write shifts MSB-first per
// the SWD protocol convention upstream uses. read returns the bits
// right-aligned in the low `bit_count` bits of the return word.
void     swd_phy_write_bits(uint32_t bit_count, uint32_t data);
uint32_t swd_phy_read_bits (uint32_t bit_count);

// Drive SWCLK for `bit_count` cycles with SWDIO in hi-z (turnaround
// + idle filler). Used by SWD line reset and IDLE sequences.
void swd_phy_hiz_clocks(uint32_t bit_count);

// Direction switching. The PIO program drops sideset HIGH on SWCLK
// and re-configures SWDIO pindir per call.
void swd_phy_read_mode(void);
void swd_phy_write_mode(void);

// Optional target reset line (only meaningful if nrst_gp != NONE
// at init). assert(true) drives nRST LOW; assert(false) releases.
// reset_level() returns the current driven level (-1 if unwired).
void swd_phy_assert_reset(bool asserted);
int  swd_phy_reset_level(void);

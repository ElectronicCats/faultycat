/*
 * services/swd_core/swd_phy.c — SWD physical layer (PIO bit-bang).
 *
 * Adapted from raspberrypi/debugprobe @ v2.3.0 under the upstream
 * MIT license. The original sources are vendored under
 * `third_party/debugprobe/` (read-only reference); this file is the
 * port that fits the FaultyCat v3 layered architecture.
 *
 * Upstream files this derives from:
 *   third_party/debugprobe/src/probe.c    -> swd_phy.c (this file)
 *   third_party/debugprobe/src/probe.pio  -> hand-encoded uint16_t
 *                                            program below
 *   third_party/debugprobe/include/board_pico_config.h (PROBE_IO_RAW)
 *
 * Differences from upstream worth knowing:
 *   1. SWCLK / SWDIO / nRST are RUNTIME parameters. v2.x routes SWD
 *      over the scanner header (Conn_01x10, GP0..GP7); the operator
 *      picks which two channels are SWCLK/SWDIO at session start.
 *      No PROBE_PIN_OFFSET compile-time constant.
 *   2. PIO instance is pio1/SM0 (pio0 is saturated by the EMFI and
 *      crowbar glitch engines, frozen at F4-1 / F5-2).
 *   3. No FreeRTOS dependency. probe_info / probe_debug / probe_dump
 *      were removed; logging (when wanted) goes through diag_printf
 *      from main.c into the CDC2 scanner shell.
 *   4. No DEBUG_PINS instrumentation (was for upstream profiling).
 *   5. probe_wait_idle() was dropped — the next blocking put()
 *      naturally orders behind any in-flight command.
 *
 * --- MIT License (debugprobe excerpt) -------------------------------
 *   The MIT License (MIT)
 *   Copyright (c) 2021-2023 Raspberry Pi (Trading) Ltd.
 *
 *   Permission is hereby granted, free of charge, to any person
 *   obtaining a copy of this software and associated documentation
 *   files (the "Software"), to deal in the Software without
 *   restriction, including without limitation the rights to use,
 *   copy, modify, merge, publish, distribute, sublicense, and/or
 *   sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following
 *   conditions:
 *
 *   The above copyright notice and this permission notice shall be
 *   included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 *   OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 *   HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 *   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 * --------------------------------------------------------------------
 *
 * Wrapping C glue (init/deinit/runtime-pin handling) is BSD-3 to
 * match the rest of the FaultyCat codebase. The encoded PIO program
 * below is the MIT-licensed material.
 */

#include "swd_phy.h"

#include "hal/gpio.h"
#include "hal/pio.h"

// ---------------------------------------------------------------------------
// PIO program — derived from third_party/debugprobe/src/probe.pio
// (PROBE_IO_RAW, .side_set 1 opt) but with SWDIO modified to use
// **open-drain emulation** for compatibility with auto-direction
// level shifters like the TXS0108EPW on FaultyCat v2.x scanner
// header. Push-pull bidirectional drive contends with the level
// shifter's one-shot accelerators when target tries to pull SWDIO
// LOW during ACK; emulating open-drain (release for HIGH, drive
// for LOW) avoids the contention since the shifter only ever sees
// one side actively driving low at a time.
//
// Differences from upstream probe.pio (PROBE_IO_RAW):
//   - write_bitloop's OUT instruction targets PINDIRS instead of PINS.
//   - Pin output is preset to 0 once at swd_phy_init via SET PINS,0.
//   - Host inverts data bits before pushing to the FIFO so the wire
//     pattern matches push-pull semantics callers expect:
//       wire HIGH ⇒ pindir 0 ⇒ release, pull-up wins
//       wire LOW  ⇒ pindir 1 ⇒ drive 0
//
//   addr  mnemonic                       opcode  notes
//    0    pull                           0x80A0  write_cmd / turnaround_cmd
//    1    out pindirs, 1 side 0 [1]      0x7181  write_bitloop  ← OD: pindir = ~bit
//    2    jmp x-- 1      side 1 [1]      0x1941
//    3    pull           side 0          0x90A0  get_next_cmd  (wrap_target)
//    4    out x, 8                       0x6028
//    5    out pindirs, 1                 0x6081  dispatcher direction setup
//    6    out pc, 5                      0x60A5
//    7    nop                            0xA042  read_bitloop
//    8    in pins, 1     side 1 [1]      0x5901  read_cmd
//    9    jmp x-- 7      side 0          0x1047
//   10    push                           0x8020  (wrap)
//
// Earlier hand-encoding bugs corrected (verified against pioasm
// output of the upstream probe.pio):
//   - instr 2 sideset bit_count must be 2 with `.side_set 1 opt`
//     (1 value bit + 1 enable bit). Was 0x0CA1, now 0x1941.
//   - HAL pio_sm_configure was passing sideset_pin_count alone
//     (without the +1 for `optional`); fixed in hal/src/rp2040/pio.c.
//
// SWCLK remains push-pull (host always drives, target never does)
// so the level shifter handles SWCLK correctly without OD emulation.
//
// FIFO command word (host-supplied):
//   bits 13..9 : cmd_addr (absolute PC = offset + label_offset)
//   bit  8     : SWDIO direction at dispatcher (1 = output / will be
//                overridden per-bit by write_bitloop, 0 = input/hi-z)
//   bits 7..0  : bit_count - 1     (CMD_SKIP uses count=1 → encoded 0)
// ---------------------------------------------------------------------------

#define SWD_PIO_PROG_LEN  11u

static const uint16_t s_prog[SWD_PIO_PROG_LEN] = {
    /* 0  */ 0x80A0u,  // pull (write_cmd / turnaround_cmd)
    /* 1  */ 0x7181u,  // out pindirs, 1 side 0 [1]   ← OD emulation
    /* 2  */ 0x1941u,  // jmp x-- 1 side 1 [1]
    /* 3  */ 0x90A0u,  // pull side 0       <-- wrap_target (get_next_cmd)
    /* 4  */ 0x6028u,  // out x, 8
    /* 5  */ 0x6081u,  // out pindirs, 1
    /* 6  */ 0x60A5u,  // out pc, 5
    /* 7  */ 0xA042u,  // nop  (read_bitloop)
    /* 8  */ 0x5901u,  // in pins, 1 [1] side 1   (read_cmd)
    /* 9  */ 0x1047u,  // jmp x-- 7 side 0
    /*10  */ 0x8020u,  // push                  <-- wrap
};

#define SWD_OFF_WRITE_CMD       0u
#define SWD_OFF_TURNAROUND_CMD  0u
#define SWD_OFF_GET_NEXT_CMD    3u
#define SWD_OFF_READ_CMD        8u
#define SWD_WRAP_TARGET_OFF     3u
#define SWD_WRAP_END_OFF        10u

// SWCLK period = 4 PIO instruction cycles per bit
// (write_bitloop = 2-cycle OUT + 2-cycle JMP, side-set toggling SWCLK).
#define SWD_PIO_CYCLES_PER_BIT  4u

// sysclk assumption: 125 MHz (default RP2040 boot clock; matches the
// EMFI and crowbar PIO programs which compile their tick budgets at
// the same divisor). If app code reclocks the system, reinit
// swd_phy with the new effective freq via swd_phy_set_clk_khz.
#define SWD_SYSCLK_KHZ          125000u

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static hal_pio_inst_t *s_pio  = NULL;
static const uint32_t  s_sm   = 0u;   // pio1/SM0 — frozen at F6-2
static uint32_t        s_off  = 0u;
static bool            s_init = false;
static uint8_t         s_swclk;
static uint8_t         s_swdio;
static int8_t          s_nrst;        // SWD_PHY_NRST_NONE if unused

typedef enum {
    CMD_WRITE      = 0,
    CMD_SKIP       = 1,
    CMD_TURNAROUND = 2,
    CMD_READ       = 3,
} swd_cmd_t;

static inline uint32_t fmt_cmd(uint32_t bit_count, bool out_en, swd_cmd_t cmd) {
    uint32_t addr_off =
        (cmd == CMD_WRITE)      ? SWD_OFF_WRITE_CMD :
        (cmd == CMD_SKIP)       ? SWD_OFF_GET_NEXT_CMD :
        (cmd == CMD_TURNAROUND) ? SWD_OFF_TURNAROUND_CMD :
                                  SWD_OFF_READ_CMD;
    uint32_t pc = s_off + addr_off;
    return ((bit_count - 1u) & 0xFFu) | ((uint32_t)out_en << 8) | (pc << 9);
}

static uint32_t khz_to_divider(uint32_t khz) {
    // From debugprobe::probe_set_swclk_freq — round up so requested
    // SWCLK is the upper bound, never exceeded.
    uint32_t div = (((SWD_SYSCLK_KHZ + khz - 1u) / khz)
                    + (SWD_PIO_CYCLES_PER_BIT - 1u))
                 / SWD_PIO_CYCLES_PER_BIT;
    if (div == 0u)     div = 1u;
    if (div > 0xFFFFu) div = 0xFFFFu;
    return div;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool swd_phy_init(uint8_t swclk_gp, uint8_t swdio_gp, int8_t nrst_gp) {
    if (s_init) return false;
    if (swclk_gp >= 30u || swdio_gp >= 30u) return false;
    if (swclk_gp == swdio_gp) return false;
    s_pio = hal_pio_instance(1);  // pio1
    if (!s_pio) return false;
    if (!hal_pio_claim_sm(s_pio, s_sm)) return false;

    hal_pio_program_t prog = { .instructions = s_prog,
                               .length       = SWD_PIO_PROG_LEN,
                               .origin       = -1 };
    if (!hal_pio_add_program(s_pio, &prog, &s_off)) {
        hal_pio_unclaim_sm(s_pio, s_sm);
        return false;
    }

    // Pin function: PIO owns SWCLK + SWDIO. SWDIO sits on a pull-up
    // so the line idles HIGH per ADIv5.
    hal_pio_gpio_init(s_pio, swclk_gp);
    hal_pio_gpio_init(s_pio, swdio_gp);
    hal_gpio_set_pulls(swdio_gp, true, false);

    // Both pins start as outputs (the program flips SWDIO direction
    // per command via OUT PINDIRS). We have to call this once per
    // pin since the pins are not necessarily adjacent.
    hal_pio_set_consecutive_pindirs(s_pio, s_sm, swclk_gp, 1, true);
    hal_pio_set_consecutive_pindirs(s_pio, s_sm, swdio_gp, 1, true);

    if (nrst_gp >= 0) {
        // Open-drain emulation: idle = input + pull-up; on assert,
        // flip to output LOW.
        hal_gpio_init((hal_gpio_pin_t)nrst_gp, HAL_GPIO_DIR_IN);
        hal_gpio_set_pulls((hal_gpio_pin_t)nrst_gp, true, false);
    }

    hal_pio_sm_cfg_t cfg = {
        .set_pin_base       = swdio_gp,
        .set_pin_count      = 1,
        .out_pin_base       = swdio_gp,
        .out_pin_count      = 1,
        .sideset_pin_base   = swclk_gp,
        .sideset_pin_count  = 1,
        .sideset_optional   = true,
        .sideset_pindirs    = false,
        .in_pin_base        = swdio_gp,
        .in_pin_count       = 1,
        .wrap_target        = SWD_WRAP_TARGET_OFF,
        .wrap_end           = SWD_WRAP_END_OFF,
        .out_shift_right    = true,   // SWD is LSB-first on the wire
        .in_shift_right     = true,
        .clk_div            = (float)khz_to_divider(SWD_PHY_CLK_DEFAULT_KHZ),
    };
    hal_pio_sm_configure(s_pio, s_sm, s_off, &cfg);
    hal_pio_sm_clear_fifos(s_pio, s_sm);

    // Open-drain emulation: preset SWDIO pin output to 0 once, then
    // never touch it again. The bitloop's `out pindirs, 1` toggles
    // pindir per bit so the line is either driven LOW (pindir=1 with
    // pin=0) or hi-Z + pulled HIGH externally (pindir=0).
    // SET PINS,0 encoding: 111_DDDDD_AAA_VVVVV with dest=000 (PINS),
    // value=00000, no sideset/delay → 0xE000.
    hal_pio_sm_exec(s_pio, s_sm, 0xE000u);

    // hal_pio_sm_configure exec'd JMP offset+0 (write_cmd, blocking
    // pull). Re-aim at get_next_cmd before enabling so the first
    // FIFO entry is treated as a command, not as data.
    // JMP-always encoding (no sideset, no delay): 0x0000 | (addr & 0x1F).
    uint16_t jmp_to_dispatcher =
        (uint16_t)((s_off + SWD_OFF_GET_NEXT_CMD) & 0x1Fu);
    hal_pio_sm_exec(s_pio, s_sm, jmp_to_dispatcher);

    hal_pio_sm_set_enabled(s_pio, s_sm, true);

    s_swclk = swclk_gp;
    s_swdio = swdio_gp;
    s_nrst  = nrst_gp;
    s_init  = true;
    return true;
}

void swd_phy_deinit(void) {
    if (!s_init) return;
    hal_pio_sm_set_enabled(s_pio, s_sm, false);
    hal_pio_program_t prog = { .instructions = s_prog,
                               .length       = SWD_PIO_PROG_LEN,
                               .origin       = -1 };
    hal_pio_remove_program(s_pio, &prog, s_off);
    hal_pio_unclaim_sm(s_pio, s_sm);

    // Restore the pins so scanner_io / jtag_core can re-claim.
    hal_gpio_init(s_swclk, HAL_GPIO_DIR_IN);
    hal_gpio_init(s_swdio, HAL_GPIO_DIR_IN);
    hal_gpio_set_pulls(s_swdio, false, false);
    if (s_nrst >= 0) {
        hal_gpio_init((hal_gpio_pin_t)s_nrst, HAL_GPIO_DIR_IN);
        hal_gpio_set_pulls((hal_gpio_pin_t)s_nrst, false, false);
    }

    s_pio  = NULL;
    s_init = false;
}

void swd_phy_set_clk_khz(uint32_t khz) {
    if (!s_init) return;
    if (khz < SWD_PHY_CLK_MIN_KHZ) khz = SWD_PHY_CLK_MIN_KHZ;
    if (khz > SWD_PHY_CLK_MAX_KHZ) khz = SWD_PHY_CLK_MAX_KHZ;
    hal_pio_sm_set_clkdiv_int(s_pio, s_sm, khz_to_divider(khz));
}

// Bounded TX/RX polls. The SM should always consume the FIFO within
// a couple of µs (4 PIO ticks per SWCLK bit at 125 MHz / 32 ≈ 1 µs
// per bit, FIFO depth 4). If something has stalled the SM (bad
// program load, runaway pin function, target shorted), the unbounded
// debugprobe-style put_blocking / read spin would hang the main loop
// — and tud_task() with it, killing magic-baud BOOTSEL. Bound at
// 10k iterations (~80 µs at 125 MHz CPU per call) so a stall costs
// the operator a single SWD command but not the firmware.
#define SWD_PHY_BOUND  10000

void swd_phy_write_bits(uint32_t bit_count, uint32_t data) {
    if (!s_init || bit_count == 0u || bit_count > 32u) return;
    uint32_t cmd = fmt_cmd(bit_count, true, CMD_WRITE);
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_put(s_pio, s_sm, cmd)) goto cmd_sent;
    }
    return;   // SM stalled before we could send the command
cmd_sent:
    // Open-drain inversion: bitloop's `out pindirs, 1` interprets each
    // shifted bit as the SWDIO direction (1=drive_LOW, 0=release/HIGH).
    // Callers think push-pull (1=HIGH, 0=LOW), so XOR-invert here so
    // the wire pattern matches what they intended.
    uint32_t inv = data ^ 0xFFFFFFFFu;
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_put(s_pio, s_sm, inv)) return;
    }
}

uint32_t swd_phy_read_bits(uint32_t bit_count) {
    if (!s_init || bit_count == 0u || bit_count > 32u) return 0u;
    uint32_t cmd = fmt_cmd(bit_count, false, CMD_READ);
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_put(s_pio, s_sm, cmd)) goto cmd_sent;
    }
    return 0u;
cmd_sent: ;
    uint32_t word = 0u;
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_get(s_pio, s_sm, &word)) {
            if (bit_count < 32u) word >>= (32u - bit_count);
            return word;
        }
    }
    // Caller's ACK read sees 0b000, which swd_dp maps to NO_TARGET.
    return 0u;
}

void swd_phy_hiz_clocks(uint32_t bit_count) {
    if (!s_init || bit_count == 0u || bit_count > 256u) return;
    uint32_t cmd = fmt_cmd(bit_count, false, CMD_TURNAROUND);
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_put(s_pio, s_sm, cmd)) goto cmd_sent;
    }
    return;
cmd_sent:
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_put(s_pio, s_sm, 0u)) return;
    }
}

void swd_phy_read_mode(void) {
    if (!s_init) return;
    // CMD_SKIP must use bit_count=1 so fmt_cmd's (count-1) is 0; a
    // bit_count of 0 would underflow to 0xFF and the dispatcher
    // would shift 256 bits before returning.
    uint32_t cmd = fmt_cmd(1u, false, CMD_SKIP);
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_put(s_pio, s_sm, cmd)) return;
    }
}

void swd_phy_write_mode(void) {
    if (!s_init) return;
    uint32_t cmd = fmt_cmd(1u, true, CMD_SKIP);
    for (int i = 0; i < SWD_PHY_BOUND; i++) {
        if (hal_pio_sm_try_put(s_pio, s_sm, cmd)) return;
    }
}

void swd_phy_assert_reset(bool asserted) {
    if (!s_init || s_nrst < 0) return;
    if (asserted) {
        hal_gpio_init((hal_gpio_pin_t)s_nrst, HAL_GPIO_DIR_OUT);
        hal_gpio_put((hal_gpio_pin_t)s_nrst, false);
    } else {
        hal_gpio_init((hal_gpio_pin_t)s_nrst, HAL_GPIO_DIR_IN);
        hal_gpio_set_pulls((hal_gpio_pin_t)s_nrst, true, false);
    }
}

int swd_phy_reset_level(void) {
    if (!s_init || s_nrst < 0) return -1;
    return hal_gpio_get((hal_gpio_pin_t)s_nrst) ? 1 : 0;
}

#include "crowbar_pio.h"

#include "board_v2.h"
#include "hal/pio.h"

// ---------------------------------------------------------------------------
// PIO instruction encodings (RP2040 datasheet §3.4)
//
// Same opcode set as services/glitch_engine/emfi/emfi_pio with one
// exception: this program raises IRQ 1 instead of IRQ 0 so the
// crowbar fire path can coexist with EMFI on the same pio0 instance
// without sharing an interrupt flag.
// ---------------------------------------------------------------------------

#define OP_PULL_BLOCK    0x80A0u
#define OP_OUT_Y_32      0x6040u
#define OP_WAIT_0_PIN0   0x2020u
#define OP_WAIT_1_PIN0   0x20A0u
#define OP_SET_PIN_HIGH  0xE001u
#define OP_SET_PIN_LOW   0xE000u
#define OP_IRQ1          0xC001u
static inline uint16_t op_jmp_y_dec(uint8_t addr) {
    return (uint16_t)(0x0080u | (addr & 0x1Fu));
}

// ---------------------------------------------------------------------------
// Clock — 125 MHz / 1.0 = 125 MHz PIO clock. 1 instr = 8 ns nominal.
// width_ns is in ns; delay_us is in microseconds (matches EMFI).
// ---------------------------------------------------------------------------
#define CROWBAR_PIO_CLK_DIV       1.0f
#define CROWBAR_PIO_NS_PER_TICK   8u
#define CROWBAR_PIO_TICKS_PER_US  125u

// ---------------------------------------------------------------------------
// Program layout (mirror of emfi_pio):
//
// [0]    PULL block                  ; pull delay_ticks into OSR
// [1]    OUT Y, 32                   ; Y = delay_ticks
// [2..N] trigger block (0..3 instrs) ; compiled from CROWBAR_TRIG_*
// [N+1]  JMP Y-- self                ; delay loop
// [N+2]  PULL block                  ; pull pulse_width_ticks
// [N+3]  OUT Y, 32                   ; Y = pulse_width_ticks
// [N+4]  SET pins=1                  ; rising edge of pulse on selected gate
// [N+5]  JMP Y-- self                ; hold high
// [N+6]  SET pins=0                  ; falling edge
// [N+7]  IRQ 1                       ; signal GLITCHED to CPU
// ---------------------------------------------------------------------------

static uint16_t s_prog[24];
static uint32_t s_prog_len;
static hal_pio_inst_t *s_pio = NULL;
static uint32_t s_sm        = 1u;     // SM 0 belongs to EMFI; crowbar uses SM 1.
static uint32_t s_offset    = 0;
static bool     s_claimed   = false;
static bool     s_loaded    = false;
static crowbar_out_t s_out  = CROWBAR_OUT_NONE;

static uint32_t s_delay_ticks;
static uint32_t s_width_ticks;

static uint32_t pin_for_output(crowbar_out_t out) {
    switch (out) {
        case CROWBAR_OUT_LP: return BOARD_GP_CROWBAR_LP;
        case CROWBAR_OUT_HP: return BOARD_GP_CROWBAR_HP;
        default:             return 0xFFFFFFFFu;
    }
}

static uint32_t compile_trigger_block(uint16_t *out, crowbar_trig_t t) {
    switch (t) {
        case CROWBAR_TRIG_IMMEDIATE:
            return 0;
        case CROWBAR_TRIG_EXT_RISING:
            out[0] = OP_WAIT_0_PIN0;
            out[1] = OP_WAIT_1_PIN0;
            return 2;
        case CROWBAR_TRIG_EXT_FALLING:
            out[0] = OP_WAIT_1_PIN0;
            out[1] = OP_WAIT_0_PIN0;
            return 2;
        case CROWBAR_TRIG_EXT_PULSE_POS:
            out[0] = OP_WAIT_0_PIN0;
            out[1] = OP_WAIT_1_PIN0;
            out[2] = OP_WAIT_0_PIN0;
            return 3;
    }
    return 0;
}

static void build_program(const crowbar_pio_params_t *p) {
    s_prog_len = 0;
    s_prog[s_prog_len++] = OP_PULL_BLOCK;
    s_prog[s_prog_len++] = OP_OUT_Y_32;
    s_prog_len += compile_trigger_block(&s_prog[s_prog_len], p->trigger);
    uint8_t delay_loop_addr = (uint8_t)s_prog_len;
    s_prog[s_prog_len++] = op_jmp_y_dec(delay_loop_addr);
    s_prog[s_prog_len++] = OP_PULL_BLOCK;
    s_prog[s_prog_len++] = OP_OUT_Y_32;
    s_prog[s_prog_len++] = OP_SET_PIN_HIGH;
    uint8_t hold_loop_addr = (uint8_t)s_prog_len;
    s_prog[s_prog_len++] = op_jmp_y_dec(hold_loop_addr);
    s_prog[s_prog_len++] = OP_SET_PIN_LOW;
    s_prog[s_prog_len++] = OP_IRQ1;
}

bool crowbar_pio_init(void) {
    s_pio = hal_pio_instance(0);
    if (!s_pio) return false;
    if (!hal_pio_claim_sm(s_pio, 1u)) return false;
    s_sm      = 1u;
    s_claimed = true;
    s_loaded  = false;
    s_out     = CROWBAR_OUT_NONE;
    return true;
}

void crowbar_pio_deinit(void) {
    if (!s_claimed) return;
    if (s_loaded) {
        hal_pio_program_t prog = { .instructions = s_prog,
                                   .length       = s_prog_len,
                                   .origin       = -1 };
        hal_pio_remove_program(s_pio, &prog, s_offset);
        s_loaded = false;
    }
    hal_pio_sm_set_enabled(s_pio, s_sm, false);
    hal_pio_unclaim_sm(s_pio, s_sm);
    s_claimed = false;
    s_pio     = NULL;
    s_out     = CROWBAR_OUT_NONE;
}

bool crowbar_pio_load(const crowbar_pio_params_t *p) {
    if (!s_claimed || !p) return false;
    if (p->output != CROWBAR_OUT_LP && p->output != CROWBAR_OUT_HP) return false;
    if (p->width_ns < CROWBAR_PIO_WIDTH_NS_MIN
     || p->width_ns > CROWBAR_PIO_WIDTH_NS_MAX) return false;
    if (p->delay_us > CROWBAR_PIO_DELAY_US_MAX) return false;

    build_program(p);
    if (s_loaded) {
        hal_pio_program_t old = { .instructions = s_prog,
                                  .length       = s_prog_len,
                                  .origin       = -1 };
        hal_pio_remove_program(s_pio, &old, s_offset);
        s_loaded = false;
    }

    hal_pio_program_t prog = { .instructions = s_prog,
                               .length       = s_prog_len,
                               .origin       = -1 };
    if (!hal_pio_add_program(s_pio, &prog, &s_offset)) return false;
    s_loaded = true;
    s_out    = p->output;

    uint32_t pin = pin_for_output(p->output);
    hal_pio_gpio_init(s_pio, pin);
    hal_pio_set_consecutive_pindirs(s_pio, s_sm, pin, 1, true);

    hal_pio_sm_cfg_t cfg = {
        .set_pin_base     = pin,
        .set_pin_count    = 1,
        .sideset_pin_base = 0,
        .sideset_pin_count= 0,
        .in_pin_base      = BOARD_GP_EXT_TRIGGER,
        .in_pin_count     = 1,
        .clk_div          = CROWBAR_PIO_CLK_DIV,
    };
    hal_pio_sm_configure(s_pio, s_sm, s_offset, &cfg);
    hal_pio_sm_clear_fifos(s_pio, s_sm);
    hal_pio_irq_clear(s_pio, 1u);

    // Cache tick counts; pushed to TX FIFO in crowbar_pio_start so the
    // program reads delay first, then width, in that order.
    s_delay_ticks = p->delay_us * CROWBAR_PIO_TICKS_PER_US;
    // Round up — width_ns of 9 should still produce at least 2 ticks
    // so the pulse hold loop runs once. Floor-by-8 would silently
    // drop sub-tick increments and surprise the operator.
    s_width_ticks = (p->width_ns + CROWBAR_PIO_NS_PER_TICK - 1u)
                  / CROWBAR_PIO_NS_PER_TICK;
    if (s_width_ticks == 0u) s_width_ticks = 1u;
    return true;
}

bool crowbar_pio_start(void) {
    if (!s_claimed || !s_loaded) return false;
    hal_pio_sm_put_blocking(s_pio, s_sm, s_delay_ticks);
    hal_pio_sm_put_blocking(s_pio, s_sm, s_width_ticks);
    hal_pio_sm_set_enabled(s_pio, s_sm, true);
    return true;
}

bool crowbar_pio_is_done(void) {
    if (!s_claimed) return false;
    return hal_pio_irq_get(s_pio, 1u);
}

void crowbar_pio_clear_done(void) {
    if (!s_claimed) return;
    hal_pio_irq_clear(s_pio, 1u);
    hal_pio_sm_set_enabled(s_pio, s_sm, false);
}

uint32_t crowbar_pio_ns_per_tick(void) {
    return CROWBAR_PIO_NS_PER_TICK;
}

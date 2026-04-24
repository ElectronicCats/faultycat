#include "emfi_pio.h"

#include "board_v2.h"
#include "emfi_pulse.h"
#include "hal/pio.h"

// ---------------------------------------------------------------------------
// PIO instruction encodings (RP2040 datasheet §3.4)
// ---------------------------------------------------------------------------

#define OP_PULL_BLOCK    0x80A0u
#define OP_OUT_Y_32      0x6040u
#define OP_WAIT_0_PIN0   0x2020u
#define OP_WAIT_1_PIN0   0x20A0u
#define OP_SET_PIN_HIGH  0xE001u
#define OP_SET_PIN_LOW   0xE000u
#define OP_IRQ0          0xC000u
static inline uint16_t op_jmp_y_dec(uint8_t addr) {
    return (uint16_t)(0x0080u | (addr & 0x1Fu));
}

// ---------------------------------------------------------------------------
// Clock — 125 MHz / 1.0 = 125 MHz PIO clock. 1 instr = 8 ns nominal,
// so 1 µs = 125 ticks.
// ---------------------------------------------------------------------------
#define EMFI_PIO_CLK_DIV 1.0f
#define EMFI_PIO_TICKS_PER_US 125u

// ---------------------------------------------------------------------------
// Program layout (up to 13 instructions, always <= 32).
//
// [0]    PULL block                  ; pull delay_ticks into OSR
// [1]    OUT Y, 32                   ; Y = delay_ticks
// [2..N] trigger block (0..3 instrs) ; compiled from EMFI_TRIG_*
// [N+1]  JMP Y-- self                ; delay loop
// [N+2]  PULL block                  ; pull pulse_width_ticks
// [N+3]  OUT Y, 32                   ; Y = pulse_width_ticks
// [N+4]  SET pins=1                  ; rising edge of pulse
// [N+5]  JMP Y-- self                ; hold high
// [N+6]  SET pins=0                  ; falling edge
// [N+7]  IRQ 0                       ; signal GLITCHED to CPU
// ---------------------------------------------------------------------------

static uint16_t s_prog[24];
static uint32_t s_prog_len;
static hal_pio_inst_t *s_pio = NULL;
static uint32_t s_sm        = 0;
static uint32_t s_offset    = 0;
static bool     s_claimed   = false;
static bool     s_loaded    = false;

static uint32_t s_delay_ticks;
static uint32_t s_width_ticks;

static uint32_t compile_trigger_block(uint16_t *out, emfi_trig_t t) {
    switch (t) {
        case EMFI_TRIG_IMMEDIATE:
            return 0;
        case EMFI_TRIG_EXT_RISING:
            out[0] = OP_WAIT_0_PIN0;
            out[1] = OP_WAIT_1_PIN0;
            return 2;
        case EMFI_TRIG_EXT_FALLING:
            out[0] = OP_WAIT_1_PIN0;
            out[1] = OP_WAIT_0_PIN0;
            return 2;
        case EMFI_TRIG_EXT_PULSE_POS:
            out[0] = OP_WAIT_0_PIN0;
            out[1] = OP_WAIT_1_PIN0;
            out[2] = OP_WAIT_0_PIN0;
            return 3;
    }
    return 0;
}

static void build_program(const emfi_pio_params_t *p) {
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
    s_prog[s_prog_len++] = OP_IRQ0;
}

bool emfi_pio_init(void) {
    s_pio = hal_pio_instance(0);
    if (!s_pio) return false;
    if (!hal_pio_claim_sm(s_pio, 0)) return false;
    s_sm      = 0;
    s_claimed = true;
    s_loaded  = false;
    return true;
}

void emfi_pio_deinit(void) {
    if (!s_claimed) return;
    // Detach the driver BEFORE unclaiming the SM — otherwise the
    // driver would stay marked attached while the PIO path is gone,
    // leaving the CPU fire path permanently refused.
    emfi_pulse_detach_pio();
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
}

bool emfi_pio_load(const emfi_pio_params_t *p) {
    if (!s_claimed || !p) return false;
    if (p->width_us < EMFI_PULSE_MIN_WIDTH_US
     || p->width_us > EMFI_PULSE_MAX_WIDTH_US) return false;

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

    // Attach GP14 to PIO and bind GP8 as in-pin for trigger waits.
    hal_pio_gpio_init(s_pio, BOARD_GP_HV_PULSE);
    hal_pio_set_consecutive_pindirs(s_pio, s_sm, BOARD_GP_HV_PULSE, 1, true);

    if (!emfi_pulse_attach_pio(s_pio, s_sm)) {
        hal_pio_remove_program(s_pio, &prog, s_offset);
        s_loaded = false;
        return false;
    }

    hal_pio_sm_cfg_t cfg = {
        .set_pin_base     = BOARD_GP_HV_PULSE,
        .set_pin_count    = 1,
        .sideset_pin_base = 0,
        .sideset_pin_count= 0,
        .in_pin_base      = BOARD_GP_EXT_TRIGGER,
        .in_pin_count     = 1,
        .clk_div          = EMFI_PIO_CLK_DIV,
    };
    hal_pio_sm_configure(s_pio, s_sm, s_offset, &cfg);
    hal_pio_sm_clear_fifos(s_pio, s_sm);
    hal_pio_irq_clear(s_pio, 0);

    // Cache tick counts; pushed to TX FIFO in emfi_pio_start so the
    // program reads delay first, then width, in that order.
    s_delay_ticks = p->delay_us * EMFI_PIO_TICKS_PER_US;
    s_width_ticks = p->width_us * EMFI_PIO_TICKS_PER_US;
    return true;
}

bool emfi_pio_start(void) {
    if (!s_claimed || !s_loaded) return false;
    hal_pio_sm_put_blocking(s_pio, s_sm, s_delay_ticks);
    hal_pio_sm_put_blocking(s_pio, s_sm, s_width_ticks);
    hal_pio_sm_set_enabled(s_pio, s_sm, true);
    return true;
}

bool emfi_pio_is_done(void) {
    if (!s_claimed) return false;
    return hal_pio_irq_get(s_pio, 0);
}

void emfi_pio_clear_done(void) {
    if (!s_claimed) return;
    hal_pio_irq_clear(s_pio, 0);
    hal_pio_sm_set_enabled(s_pio, s_sm, false);
}

uint32_t emfi_pio_ticks_per_us(void) {
    return EMFI_PIO_TICKS_PER_US;
}

#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/glitch_engine/crowbar/crowbar_pio — compiled PIO program
// that drives ONE crowbar gate (LP on GP17 or HP on GP16) after an
// optional external trigger and a configurable delay. The selected
// gate is pulled HIGH for `width_ns`, then LOW, then IRQ 1 fires.
//
// Reimplemented from scratch under BSD-3. The faultier
// trigger/delay/glitch compiler architecture inspired the layered
// approach; no line of code is copied from hextreeio/faultier
// (unlicensed upstream).
//
// PIO allocation: pio0, SM 1. SM 0 belongs to
// services/glitch_engine/emfi/emfi_pio. ARCHITECTURE.md documents
// the repo-wide PIO instance convention.
//
// IRQ allocation: IRQ 1 (EMFI uses IRQ 0).

// Trigger polarity. The PIO program for each option is described in
// services/glitch_engine/crowbar/crowbar_pio.c::compile_trigger_block.
//
//   IMMEDIATE    → no trigger; fires as soon as PIO is started.
//   EXT_RISING   → Program: WAIT 0, WAIT 1. Last event = rising edge.
//   EXT_FALLING  → Program: WAIT 1, WAIT 0. Last event = falling edge.
//   EXT_PULSE_POS→ Program: WAIT 0, WAIT 1, WAIT 0. Trigger source
//                  generates LOW→HIGH→LOW; fire after the trailing
//                  falling edge.
//   EXT_PULSE_NEG→ Program: WAIT 1, WAIT 0, WAIT 1. Trigger source
//                  generates HIGH→LOW→HIGH; fire after the trailing
//                  rising edge — inverse of PULSE_POS.
//
// The GP8 trigger line idle level is fixed at boot by
// ext_trigger_init(EXT_TRIGGER_PULL_DOWN) in apps/faultycat_fw/main.c
// — the service layer does not flip it per arm. EXT_FALLING and
// EXT_PULSE_NEG therefore require the operator's external source to
// hold the line HIGH between events; otherwise the leading WAIT 1
// stalls.
typedef enum {
    CROWBAR_TRIG_IMMEDIATE     = 0,
    CROWBAR_TRIG_EXT_RISING    = 1,
    CROWBAR_TRIG_EXT_FALLING   = 2,
    CROWBAR_TRIG_EXT_PULSE_POS = 3,
    CROWBAR_TRIG_EXT_PULSE_NEG = 4,
} crowbar_trig_t;

typedef enum {
    CROWBAR_OUT_NONE = 0,
    CROWBAR_OUT_LP   = 1,
    CROWBAR_OUT_HP   = 2,
} crowbar_out_t;

// width_ns minimum is one PIO tick (8 ns @ 125 MHz). Maximum capped
// at 50 µs to keep voltage glitching from cooking the target.
#define CROWBAR_PIO_WIDTH_NS_MIN 8u
#define CROWBAR_PIO_WIDTH_NS_MAX 50000u

// delay_us range mirrors EMFI: 0 (immediate) up to 1 s.
#define CROWBAR_PIO_DELAY_US_MAX 1000000u

typedef struct {
    crowbar_trig_t trigger;
    crowbar_out_t output; // which gate the PIO drives this fire
    uint32_t delay_us;    // 0..CROWBAR_PIO_DELAY_US_MAX
    uint32_t width_ns;    // CROWBAR_PIO_WIDTH_NS_MIN..MAX
} crowbar_pio_params_t;

// One-time init. Claims pio0/SM1. Returns false if SM is already
// claimed elsewhere.
bool crowbar_pio_init(void);

// Release pio0/SM1 and clear its instruction memory. Safe to call
// repeatedly. Restores the chosen gate to a plain GPIO (LOW) so the
// driver/crowbar_mosfet can re-assume ownership cleanly.
void crowbar_pio_deinit(void);

// Compile `params` into a PIO program and load it. On success the SM
// is configured (but NOT enabled) and the delay/width tick counts
// have been pushed to the TX FIFO in the order the program expects.
// The selected output gate (LP or HP) is attached to the PIO instance
// here; CROWBAR_OUT_NONE is rejected as a config error.
bool crowbar_pio_load(const crowbar_pio_params_t* params);

// Enable the SM; after this the program starts executing and will
// eventually raise IRQ 1 when the pulse has fired.
bool crowbar_pio_start(void);

// Poll IRQ 1 — true once the program has raised the "glitched" IRQ.
bool crowbar_pio_is_done(void);

// Clear IRQ 1 so the next start() sees a fresh line.
void crowbar_pio_clear_done(void);

// Nanoseconds per PIO tick at the compile-time clock divisor. For the
// fixed 125 MHz / 1.0 setup this returns 8 (i.e. one tick = 8 ns).
// Exposed for tests and for the campaign layer's status reports.
uint32_t crowbar_pio_ns_per_tick(void);

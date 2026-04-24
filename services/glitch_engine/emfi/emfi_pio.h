#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/glitch_engine/emfi/emfi_pio — compiled PIO program that
// drives GP14 (HV_PULSE) after an optional external trigger and a
// configurable delay.
//
// Reimplemented from scratch under BSD-3. The faultier trigger/delay/
// glitch compiler architecture inspired the layered approach; no line
// of code is copied from hextreeio/faultier (unlicensed upstream).
//
// PIO allocation: pio0, SM 0. ARCHITECTURE.md documents the repo-
// wide PIO instance convention (pio0 for glitch engines; pio1 for
// SWD/UART/JTAG).

typedef enum {
    EMFI_TRIG_IMMEDIATE     = 0,
    EMFI_TRIG_EXT_RISING    = 1,
    EMFI_TRIG_EXT_FALLING   = 2,
    EMFI_TRIG_EXT_PULSE_POS = 3,
} emfi_trig_t;

typedef struct {
    emfi_trig_t trigger;
    uint32_t    delay_us;      // 0..1_000_000
    uint32_t    width_us;      // 1..50 (mirrors EMFI_PULSE_MIN/MAX)
} emfi_pio_params_t;

// One-time init. Claims pio0/SM0 and prepares the instance. Returns
// false if the SM is already claimed elsewhere.
bool emfi_pio_init(void);

// Release pio0/SM0 and clear its instruction memory. Safe to call
// repeatedly.
void emfi_pio_deinit(void);

// Compile `params` into a PIO program and load it. On success the SM
// is configured (but NOT enabled) and the delay/width words have been
// pushed to the TX FIFO in the order the program expects.
bool emfi_pio_load(const emfi_pio_params_t *params);

// Enable the SM; after this the program starts executing and will
// eventually raise IRQ 0 when the pulse has fired.
bool emfi_pio_start(void);

// Poll IRQ 0 — true once the program has raised the "glitched" IRQ.
bool emfi_pio_is_done(void);

// Clear IRQ 0 so the next start() sees a fresh line.
void emfi_pio_clear_done(void);

// Convert microseconds to PIO ticks at the configured clock divisor.
// Exposed for tests and for the campaign layer's status reports.
uint32_t emfi_pio_ticks_per_us(void);

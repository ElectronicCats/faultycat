#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// HAL — PIO
//
// Portable access to the RP2040 PIO block: state machines, programs,
// FIFOs, IRQs, and the minimal pin-binding surface callers need.
//
// Scope: everything the EMFI trigger/delay/pulse compiler and the
// crowbar glitcher need. SWD/UART/JTAG PIO work in later phases will
// extend this header as new primitives are demanded.

typedef struct hal_pio_inst hal_pio_inst_t;  // opaque, one per PIO block

typedef struct {
    const uint16_t *instructions;  // raw PIO instructions
    uint32_t        length;        // instruction count
    int32_t         origin;        // -1 = anywhere, else forced offset
} hal_pio_program_t;

typedef struct {
    uint32_t set_pin_base;
    uint32_t set_pin_count;
    uint32_t sideset_pin_base;
    uint32_t sideset_pin_count;
    uint32_t in_pin_base;
    float    clk_div;              // 1.0 == sysclock
} hal_pio_sm_cfg_t;

// Obtain a handle to PIO block `which` (0 or 1). Returns NULL if the
// argument is out of range.
hal_pio_inst_t *hal_pio_instance(uint8_t which);

// True if `program` can be loaded into `pio` at this moment.
bool hal_pio_can_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *program);

// Load `program`. Writes the chosen offset into `*offset_out` on
// success. Returns false if no room.
bool hal_pio_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *program,
                        uint32_t *offset_out);

// Remove a previously-loaded program at `offset`.
void hal_pio_remove_program(hal_pio_inst_t *pio, const hal_pio_program_t *program,
                           uint32_t offset);

// Wipe all loaded instructions. Use sparingly — invalidates every
// offset returned by add_program.
void hal_pio_clear_instruction_memory(hal_pio_inst_t *pio);

// Claim state machine `sm` (0..3). Returns false if already claimed.
bool hal_pio_claim_sm(hal_pio_inst_t *pio, uint32_t sm);
void hal_pio_unclaim_sm(hal_pio_inst_t *pio, uint32_t sm);

// Apply `cfg` and the loaded program offset to state machine `sm`.
// Leaves the SM disabled; caller enables explicitly.
void hal_pio_sm_configure(hal_pio_inst_t *pio, uint32_t sm, uint32_t offset,
                         const hal_pio_sm_cfg_t *cfg);

void hal_pio_sm_set_enabled(hal_pio_inst_t *pio, uint32_t sm, bool enabled);
void hal_pio_sm_clear_fifos(hal_pio_inst_t *pio, uint32_t sm);
void hal_pio_sm_restart(hal_pio_inst_t *pio, uint32_t sm);

// Blocks until the TX FIFO can accept `word`.
void hal_pio_sm_put_blocking(hal_pio_inst_t *pio, uint32_t sm, uint32_t word);
// Non-blocking put; returns false if TX FIFO full.
bool hal_pio_sm_try_put(hal_pio_inst_t *pio, uint32_t sm, uint32_t word);
// Non-blocking get; returns false if RX FIFO empty.
bool hal_pio_sm_try_get(hal_pio_inst_t *pio, uint32_t sm, uint32_t *out);

// Read / clear one of the 8 PIO IRQ lines (0..7). The PIO program
// raises these with IRQ instructions; CPU polls from the main loop.
bool hal_pio_irq_get(hal_pio_inst_t *pio, uint32_t irq_index);
void hal_pio_irq_clear(hal_pio_inst_t *pio, uint32_t irq_index);

// Route GPIO `gpio` to PIO control. Must precede any program output
// onto that pin.
void hal_pio_gpio_init(hal_pio_inst_t *pio, uint32_t gpio);

// Set direction of `count` consecutive pins starting at `base`. When
// is_out is true, the SM drives them as outputs; false leaves them
// floating inputs.
void hal_pio_set_consecutive_pindirs(hal_pio_inst_t *pio, uint32_t sm,
                                    uint32_t base, uint32_t count, bool is_out);

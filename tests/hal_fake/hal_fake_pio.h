#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/pio.h"

#define HAL_FAKE_PIO_INSTANCES    2
#define HAL_FAKE_PIO_SM_PER_INST  4
#define HAL_FAKE_PIO_IRQ_COUNT    8
#define HAL_FAKE_PIO_FIFO_DEPTH   16
#define HAL_FAKE_PIO_PROGRAM_MAX  32

typedef struct {
    bool      claimed;
    bool      enabled;
    uint32_t  configured_offset;
    hal_pio_sm_cfg_t last_cfg;
    uint32_t  configure_calls;
    uint32_t  enable_calls;
    uint32_t  clear_fifo_calls;
    uint32_t  restart_calls;
    uint32_t  tx_fifo[HAL_FAKE_PIO_FIFO_DEPTH];
    uint32_t  tx_count;
    uint32_t  rx_fifo[HAL_FAKE_PIO_FIFO_DEPTH];
    uint32_t  rx_count;
    uint32_t  pindirs_calls;
    uint32_t  last_pindirs_base;
    uint32_t  last_pindirs_count;
    bool      last_pindirs_is_out;
    // Added F6-2 for swd_phy.
    uint32_t  exec_calls;
    uint16_t  last_exec_instr;
    uint32_t  set_clkdiv_int_calls;
    uint32_t  last_clkdiv_int;
} hal_fake_pio_sm_state_t;

typedef struct {
    uint16_t  instructions[HAL_FAKE_PIO_PROGRAM_MAX];
    uint32_t  length;
    uint32_t  base_offset;
    bool      loaded;
} hal_fake_pio_program_slot_t;

typedef struct {
    hal_fake_pio_sm_state_t     sm[HAL_FAKE_PIO_SM_PER_INST];
    hal_fake_pio_program_slot_t program;   // single-slot model is enough for F4
    bool                         irq[HAL_FAKE_PIO_IRQ_COUNT];
    uint32_t                     gpio_init_bitmap;  // bit N = gpio_init called for pin N
    uint32_t                     clear_memory_calls;
} hal_fake_pio_inst_state_t;

extern hal_fake_pio_inst_state_t hal_fake_pio_insts[HAL_FAKE_PIO_INSTANCES];

void hal_fake_pio_reset(void);

// Test-only hook: push `word` onto SM `sm`'s RX FIFO so try_get can
// return it. Used by tests that simulate PIO → CPU traffic.
void hal_fake_pio_push_rx(uint8_t which, uint32_t sm, uint32_t word);

// Test-only hook: raise IRQ `irq_index` so hal_pio_irq_get returns true.
void hal_fake_pio_raise_irq(uint8_t which, uint32_t irq_index);

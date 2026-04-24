#include "hal_fake_pio.h"

#include <string.h>

hal_fake_pio_inst_state_t hal_fake_pio_insts[HAL_FAKE_PIO_INSTANCES];

void hal_fake_pio_reset(void) {
    memset(hal_fake_pio_insts, 0, sizeof(hal_fake_pio_insts));
}

// The "instance" returned from hal_pio_instance is actually a pointer
// into the fake state array; callers treat it opaquely so the cast
// does not leak.
hal_pio_inst_t *hal_pio_instance(uint8_t which) {
    if (which >= HAL_FAKE_PIO_INSTANCES) return NULL;
    return (hal_pio_inst_t *)&hal_fake_pio_insts[which];
}

static hal_fake_pio_inst_state_t *as_state(hal_pio_inst_t *pio) {
    return (hal_fake_pio_inst_state_t *)pio;
}

bool hal_pio_can_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p) {
    if (!pio || !p) return false;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    if (s->program.loaded) return false;
    return p->length <= HAL_FAKE_PIO_PROGRAM_MAX;
}

bool hal_pio_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                        uint32_t *offset_out) {
    if (!hal_pio_can_add_program(pio, p)) return false;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    memcpy(s->program.instructions, p->instructions,
           p->length * sizeof(uint16_t));
    s->program.length      = p->length;
    s->program.base_offset = (p->origin >= 0) ? (uint32_t)p->origin : 0u;
    s->program.loaded      = true;
    if (offset_out) *offset_out = s->program.base_offset;
    return true;
}

void hal_pio_remove_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                           uint32_t offset) {
    (void)p; (void)offset;
    if (!pio) return;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    memset(&s->program, 0, sizeof(s->program));
}

void hal_pio_clear_instruction_memory(hal_pio_inst_t *pio) {
    if (!pio) return;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    memset(&s->program, 0, sizeof(s->program));
    s->clear_memory_calls++;
}

bool hal_pio_claim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return false;
    hal_fake_pio_inst_state_t *s = as_state(pio);
    if (s->sm[sm].claimed) return false;
    s->sm[sm].claimed = true;
    return true;
}

void hal_pio_unclaim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    as_state(pio)->sm[sm].claimed = false;
}

void hal_pio_sm_configure(hal_pio_inst_t *pio, uint32_t sm, uint32_t offset,
                         const hal_pio_sm_cfg_t *cfg) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST || !cfg) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->configured_offset = offset;
    smst->last_cfg          = *cfg;
    smst->configure_calls++;
}

void hal_pio_sm_set_enabled(hal_pio_inst_t *pio, uint32_t sm, bool en) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->enabled = en;
    smst->enable_calls++;
}

void hal_pio_sm_clear_fifos(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->tx_count = 0;
    smst->rx_count = 0;
    smst->clear_fifo_calls++;
}

void hal_pio_sm_restart(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    as_state(pio)->sm[sm].restart_calls++;
}

void hal_pio_sm_put_blocking(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    if (smst->tx_count < HAL_FAKE_PIO_FIFO_DEPTH) {
        smst->tx_fifo[smst->tx_count++] = word;
    }
}

bool hal_pio_sm_try_put(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return false;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    if (smst->tx_count >= HAL_FAKE_PIO_FIFO_DEPTH) return false;
    smst->tx_fifo[smst->tx_count++] = word;
    return true;
}

bool hal_pio_sm_try_get(hal_pio_inst_t *pio, uint32_t sm, uint32_t *out) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST || !out) return false;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    if (smst->rx_count == 0) return false;
    *out = smst->rx_fifo[0];
    for (uint32_t i = 1; i < smst->rx_count; i++) {
        smst->rx_fifo[i - 1] = smst->rx_fifo[i];
    }
    smst->rx_count--;
    return true;
}

bool hal_pio_irq_get(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index >= HAL_FAKE_PIO_IRQ_COUNT) return false;
    return as_state(pio)->irq[irq_index];
}

void hal_pio_irq_clear(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index >= HAL_FAKE_PIO_IRQ_COUNT) return;
    as_state(pio)->irq[irq_index] = false;
}

void hal_pio_gpio_init(hal_pio_inst_t *pio, uint32_t gpio) {
    // Cap is fake-internal: (1u << 32) is UB in C, and the bitmap is
    // 32 bits wide. Real impl forwards any value to pico-sdk (asserts
    // on invalid). Tests should only pass gpio ∈ [0, 29] (RP2040 range).
    if (!pio || gpio >= 32) return;
    as_state(pio)->gpio_init_bitmap |= (1u << gpio);
}

void hal_pio_set_consecutive_pindirs(hal_pio_inst_t *pio, uint32_t sm,
                                    uint32_t base, uint32_t count, bool is_out) {
    if (!pio || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &as_state(pio)->sm[sm];
    smst->pindirs_calls++;
    smst->last_pindirs_base   = base;
    smst->last_pindirs_count  = count;
    smst->last_pindirs_is_out = is_out;
}

// Test-only hooks
void hal_fake_pio_push_rx(uint8_t which, uint32_t sm, uint32_t word) {
    if (which >= HAL_FAKE_PIO_INSTANCES || sm >= HAL_FAKE_PIO_SM_PER_INST) return;
    hal_fake_pio_sm_state_t *smst = &hal_fake_pio_insts[which].sm[sm];
    if (smst->rx_count < HAL_FAKE_PIO_FIFO_DEPTH) {
        smst->rx_fifo[smst->rx_count++] = word;
    }
}

void hal_fake_pio_raise_irq(uint8_t which, uint32_t irq_index) {
    if (which >= HAL_FAKE_PIO_INSTANCES || irq_index >= HAL_FAKE_PIO_IRQ_COUNT) return;
    hal_fake_pio_insts[which].irq[irq_index] = true;
}

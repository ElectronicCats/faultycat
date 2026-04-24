#include "hal/pio.h"

#include "hardware/pio.h"
#include "hardware/gpio.h"

// hal_pio_inst_t is really a pico-sdk PIO. Cast on the way in.
static inline PIO as_pio(hal_pio_inst_t *p) { return (PIO)p; }

hal_pio_inst_t *hal_pio_instance(uint8_t which) {
    if (which == 0) return (hal_pio_inst_t *)pio0;
    if (which == 1) return (hal_pio_inst_t *)pio1;
    return NULL;
}

bool hal_pio_can_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p) {
    if (!pio || !p) return false;
    pio_program_t prog = { .instructions = p->instructions,
                           .length       = (uint8_t)p->length,
                           .origin       = (int8_t)p->origin };
    return pio_can_add_program(as_pio(pio), &prog);
}

bool hal_pio_add_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                        uint32_t *offset_out) {
    if (!pio || !p) return false;
    pio_program_t prog = { .instructions = p->instructions,
                           .length       = (uint8_t)p->length,
                           .origin       = (int8_t)p->origin };
    int off = pio_add_program(as_pio(pio), &prog);
    if (off < 0) return false;
    if (offset_out) *offset_out = (uint32_t)off;
    return true;
}

void hal_pio_remove_program(hal_pio_inst_t *pio, const hal_pio_program_t *p,
                           uint32_t offset) {
    if (!pio || !p) return;
    pio_program_t prog = { .instructions = p->instructions,
                           .length       = (uint8_t)p->length,
                           .origin       = (int8_t)p->origin };
    pio_remove_program(as_pio(pio), &prog, (uint)offset);
}

void hal_pio_clear_instruction_memory(hal_pio_inst_t *pio) {
    if (!pio) return;
    pio_clear_instruction_memory(as_pio(pio));
}

bool hal_pio_claim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return false;
    if (pio_sm_is_claimed(as_pio(pio), (uint)sm)) return false;
    pio_sm_claim(as_pio(pio), (uint)sm);
    return true;
}

void hal_pio_unclaim_sm(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return;
    pio_sm_unclaim(as_pio(pio), (uint)sm);
}

void hal_pio_sm_configure(hal_pio_inst_t *pio, uint32_t sm, uint32_t offset,
                         const hal_pio_sm_cfg_t *cfg) {
    if (!pio || sm > 3 || !cfg) return;
    pio_sm_config c = pio_get_default_sm_config();
    if (cfg->set_pin_count) {
        sm_config_set_set_pins(&c, cfg->set_pin_base, cfg->set_pin_count);
    }
    if (cfg->sideset_pin_count) {
        sm_config_set_sideset(&c, cfg->sideset_pin_count,
                              cfg->sideset_optional, cfg->sideset_pindirs);
        sm_config_set_sideset_pins(&c, cfg->sideset_pin_base);
    }
    if (cfg->in_pin_count) {
        sm_config_set_in_pins(&c, cfg->in_pin_base);
    }
    sm_config_set_clkdiv(&c, cfg->clk_div <= 0.0f ? 1.0f : cfg->clk_div);
    pio_sm_init(as_pio(pio), (uint)sm, (uint)offset, &c);
}

void hal_pio_sm_set_enabled(hal_pio_inst_t *pio, uint32_t sm, bool en) {
    if (!pio || sm > 3) return;
    pio_sm_set_enabled(as_pio(pio), (uint)sm, en);
}

void hal_pio_sm_clear_fifos(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return;
    pio_sm_clear_fifos(as_pio(pio), (uint)sm);
}

void hal_pio_sm_restart(hal_pio_inst_t *pio, uint32_t sm) {
    if (!pio || sm > 3) return;
    pio_sm_restart(as_pio(pio), (uint)sm);
}

void hal_pio_sm_put_blocking(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm > 3) return;
    pio_sm_put_blocking(as_pio(pio), (uint)sm, word);
}

bool hal_pio_sm_try_put(hal_pio_inst_t *pio, uint32_t sm, uint32_t word) {
    if (!pio || sm > 3) return false;
    if (pio_sm_is_tx_fifo_full(as_pio(pio), (uint)sm)) return false;
    pio_sm_put(as_pio(pio), (uint)sm, word);
    return true;
}

bool hal_pio_sm_try_get(hal_pio_inst_t *pio, uint32_t sm, uint32_t *out) {
    if (!pio || sm > 3 || !out) return false;
    if (pio_sm_is_rx_fifo_empty(as_pio(pio), (uint)sm)) return false;
    *out = pio_sm_get(as_pio(pio), (uint)sm);
    return true;
}

bool hal_pio_irq_get(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index > 7) return false;
    return pio_interrupt_get(as_pio(pio), (uint)irq_index);
}

void hal_pio_irq_clear(hal_pio_inst_t *pio, uint32_t irq_index) {
    if (!pio || irq_index > 7) return;
    pio_interrupt_clear(as_pio(pio), (uint)irq_index);
}

void hal_pio_gpio_init(hal_pio_inst_t *pio, uint32_t gpio) {
    if (!pio) return;
    pio_gpio_init(as_pio(pio), (uint)gpio);
}

void hal_pio_set_consecutive_pindirs(hal_pio_inst_t *pio, uint32_t sm,
                                    uint32_t base, uint32_t count, bool is_out) {
    if (!pio || sm > 3) return;
    pio_sm_set_consecutive_pindirs(as_pio(pio), (uint)sm, (uint)base,
                                   (uint)count, is_out);
}

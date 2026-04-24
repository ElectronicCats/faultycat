#include "crowbar_pio.h"

// F5-1 skeleton — API-only stub. F5-2 lands the real PIO program
// build, the gate attach, and the IRQ wiring.

bool crowbar_pio_init(void) {
    return false;
}

void crowbar_pio_deinit(void) {
}

bool crowbar_pio_load(const crowbar_pio_params_t *params) {
    (void)params;
    return false;
}

bool crowbar_pio_start(void) {
    return false;
}

bool crowbar_pio_is_done(void) {
    return false;
}

void crowbar_pio_clear_done(void) {
}

uint32_t crowbar_pio_ticks_per_8ns(void) {
    return 1u;
}

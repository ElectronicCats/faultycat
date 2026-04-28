// hal/gpio — native host fake. Implements hal_gpio_* by recording
// every call into per-pin state that tests inspect via
// hal_fake_gpio_states[pin].
//
// F8-1 extensions:
//   - per-pin input scripts (hal_gpio_get returns the next scripted bit
//     until exhausted, then falls back to states[pin].level).
//   - rising-edge sampler keyed on a single trigger pin (TCK in JTAG
//     tests), captures the levels of up to 4 watch pins into a ring.

#include "hal_fake_gpio.h"

#include <string.h>

hal_fake_gpio_state_t hal_fake_gpio_states[HAL_FAKE_GPIO_MAX_PINS];

// Per-pin input scripts. Allocating MAX_PINS × MAX_LEN bools is ~256 KB
// in worst case which is fine on the host. Cursor advances on each
// hal_gpio_get call until it hits `len`, then we fall back to `level`.
typedef struct {
    bool   bits[HAL_FAKE_GPIO_INPUT_SCRIPT_MAX];
    size_t len;
    size_t cursor;
} input_script_t;

static input_script_t s_scripts[HAL_FAKE_GPIO_MAX_PINS];

// Edge sampler — single global sampler. Configurable trigger pin +
// 4 watch pins; each rising edge of trigger appends a snapshot to a
// flat log.
static struct {
    bool                          configured;
    hal_gpio_pin_t                trigger;
    hal_gpio_pin_t                watch[4];
    bool                          last_trigger_level;
    hal_fake_gpio_edge_sample_t   log[HAL_FAKE_GPIO_EDGE_LOG_MAX];
    size_t                        count;
} s_edge;

void hal_fake_gpio_reset(void) {
    memset(hal_fake_gpio_states, 0, sizeof(hal_fake_gpio_states));
    memset(s_scripts, 0, sizeof(s_scripts));
    memset(&s_edge, 0, sizeof(s_edge));
    s_edge.trigger = HAL_FAKE_GPIO_PIN_NONE;
    for (int i = 0; i < 4; i++) s_edge.watch[i] = HAL_FAKE_GPIO_PIN_NONE;
}

// Snapshot the watch pins at this instant and append to the edge log.
// Reads each watch pin's `level` directly (NOT through hal_gpio_get,
// to avoid consuming an input-script bit on a watch pin we just want
// to observe).
static void edge_sample_snapshot(void) {
    if (!s_edge.configured) return;
    if (s_edge.count >= HAL_FAKE_GPIO_EDGE_LOG_MAX) return;
    hal_fake_gpio_edge_sample_t *e = &s_edge.log[s_edge.count++];
    for (int i = 0; i < 4; i++) {
        hal_gpio_pin_t p = s_edge.watch[i];
        e->watch[i] = (p < HAL_FAKE_GPIO_MAX_PINS) ? hal_fake_gpio_states[p].level
                                                   : false;
    }
}

void hal_gpio_init(hal_gpio_pin_t pin, hal_gpio_dir_t dir) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return;
    }
    hal_fake_gpio_state_t *s = &hal_fake_gpio_states[pin];
    s->initialized = true;
    s->dir         = dir;
    s->init_calls++;
}

void hal_gpio_put(hal_gpio_pin_t pin, bool value) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return;
    }
    bool prev = hal_fake_gpio_states[pin].level;
    hal_fake_gpio_states[pin].level = value;
    hal_fake_gpio_states[pin].put_calls++;

    // Edge sampler — fire on rising edge of the trigger pin. Done
    // after the level is updated so a watch pin == trigger sees the
    // new value (rare but useful invariant).
    if (s_edge.configured && pin == s_edge.trigger
     && !prev && value) {
        edge_sample_snapshot();
    }
}

bool hal_gpio_get(hal_gpio_pin_t pin) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return false;
    }
    hal_fake_gpio_states[pin].get_calls++;

    // If a script is loaded and not yet exhausted, return the next bit.
    input_script_t *sc = &s_scripts[pin];
    if (sc->cursor < sc->len) {
        return sc->bits[sc->cursor++];
    }
    return hal_fake_gpio_states[pin].level;
}

void hal_gpio_set_pulls(hal_gpio_pin_t pin, bool pull_up, bool pull_down) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) {
        return;
    }
    hal_fake_gpio_state_t *s = &hal_fake_gpio_states[pin];
    s->pull_up   = pull_up;
    s->pull_down = pull_down;
    s->pulls_calls++;
}

// -----------------------------------------------------------------------------
// Input script API
// -----------------------------------------------------------------------------

void hal_fake_gpio_input_script_load(hal_gpio_pin_t pin,
                                     const bool *bits, size_t bit_count) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS || bits == NULL) return;
    if (bit_count > HAL_FAKE_GPIO_INPUT_SCRIPT_MAX) {
        bit_count = HAL_FAKE_GPIO_INPUT_SCRIPT_MAX;
    }
    input_script_t *sc = &s_scripts[pin];
    memcpy(sc->bits, bits, bit_count * sizeof(bool));
    sc->len    = bit_count;
    sc->cursor = 0;
}

size_t hal_fake_gpio_input_script_remaining(hal_gpio_pin_t pin) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) return 0;
    const input_script_t *sc = &s_scripts[pin];
    return (sc->cursor < sc->len) ? (sc->len - sc->cursor) : 0u;
}

size_t hal_fake_gpio_input_script_consumed(hal_gpio_pin_t pin) {
    if (pin >= HAL_FAKE_GPIO_MAX_PINS) return 0;
    return s_scripts[pin].cursor;
}

// -----------------------------------------------------------------------------
// Edge sampler API
// -----------------------------------------------------------------------------

void hal_fake_gpio_edge_sampler_configure(hal_gpio_pin_t trigger,
                                          hal_gpio_pin_t w0,
                                          hal_gpio_pin_t w1,
                                          hal_gpio_pin_t w2,
                                          hal_gpio_pin_t w3) {
    s_edge.configured        = true;
    s_edge.trigger           = trigger;
    s_edge.watch[0]          = w0;
    s_edge.watch[1]          = w1;
    s_edge.watch[2]          = w2;
    s_edge.watch[3]          = w3;
    s_edge.last_trigger_level = (trigger < HAL_FAKE_GPIO_MAX_PINS)
                                ? hal_fake_gpio_states[trigger].level
                                : false;
    s_edge.count             = 0;
}

void hal_fake_gpio_edge_sampler_reset(void) {
    s_edge.count = 0;
}

size_t hal_fake_gpio_edge_sampler_count(void) {
    return s_edge.count;
}

hal_fake_gpio_edge_sample_t hal_fake_gpio_edge_sampler_at(size_t idx) {
    hal_fake_gpio_edge_sample_t empty = {{0}};
    if (idx >= s_edge.count) return empty;
    return s_edge.log[idx];
}

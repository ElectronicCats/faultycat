#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/gpio.h"

// Number of pins the fake tracks. RP2040 has 30 GPIOs; we round up
// to 32 so anyone stores full-byte arrays if they want to.
#define HAL_FAKE_GPIO_MAX_PINS 32

typedef struct {
    bool            initialized;
    hal_gpio_dir_t  dir;
    bool            level;
    bool            pull_up;
    bool            pull_down;
    uint32_t        init_calls;
    uint32_t        put_calls;
    uint32_t        get_calls;
    uint32_t        pulls_calls;
} hal_fake_gpio_state_t;

extern hal_fake_gpio_state_t hal_fake_gpio_states[HAL_FAKE_GPIO_MAX_PINS];

// Zero every pin's state AND clears the input-script and edge-sampler
// state. Tests should call this from setUp().
void hal_fake_gpio_reset(void);

// -----------------------------------------------------------------------------
// Input script (F8-1) — scripted return values for hal_gpio_get on a pin.
//
// When a script is loaded, hal_gpio_get(pin) returns the next bit from
// the script and advances the cursor; once exhausted it falls back to
// hal_fake_gpio_states[pin].level. Use case: simulate a target driving
// a clocked-in serial bus (TDO under JTAG, MISO under SPI). Each slot
// in the script is a single bit (true = HIGH, false = LOW).
// -----------------------------------------------------------------------------

#define HAL_FAKE_GPIO_INPUT_SCRIPT_MAX 8192u

void   hal_fake_gpio_input_script_load(hal_gpio_pin_t pin,
                                       const bool *bits, size_t bit_count);
size_t hal_fake_gpio_input_script_remaining(hal_gpio_pin_t pin);
size_t hal_fake_gpio_input_script_consumed(hal_gpio_pin_t pin);

// -----------------------------------------------------------------------------
// Edge sampler (F8-1) — when `trigger_pin` rises (0→1), snapshot up to
// four "watch" pin levels into a log entry. Use case: capture the TMS
// + TDI + (returned) TDO sequence at every TCK rising edge while a TAP
// state-machine routine runs, so the test can assert the wire trace.
//
// Pass HAL_FAKE_GPIO_PIN_NONE for a watch slot you don't need.
// -----------------------------------------------------------------------------

#define HAL_FAKE_GPIO_PIN_NONE 0xFFu
#define HAL_FAKE_GPIO_EDGE_LOG_MAX 4096u

typedef struct {
    bool watch[4];
} hal_fake_gpio_edge_sample_t;

void   hal_fake_gpio_edge_sampler_configure(hal_gpio_pin_t trigger,
                                            hal_gpio_pin_t w0,
                                            hal_gpio_pin_t w1,
                                            hal_gpio_pin_t w2,
                                            hal_gpio_pin_t w3);
void   hal_fake_gpio_edge_sampler_reset(void);
size_t hal_fake_gpio_edge_sampler_count(void);
hal_fake_gpio_edge_sample_t hal_fake_gpio_edge_sampler_at(size_t idx);

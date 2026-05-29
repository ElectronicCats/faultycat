#pragma once

#include <stdbool.h>
#include <stdint.h>

// HAL — GPIO
//
// Portable, platform-agnostic digital-I/O interface. The RP2040
// implementation lives in hal/src/rp2040/gpio.c; the test-host fake
// lives in tests/hal_fake/gpio_fake.c. Drivers and apps must never
// reach around this header to call pico-sdk gpio_* directly.
//
// Scope in F1: init / put / get / pulls. Drive strength, slew rate,
// alternate functions, and interrupt callbacks arrive in later phases
// as specific drivers need them.

typedef uint8_t hal_gpio_pin_t;

typedef enum {
    HAL_GPIO_DIR_IN  = 0,
    HAL_GPIO_DIR_OUT = 1,
} hal_gpio_dir_t;

// Configure `pin` as a plain digital I/O in the given direction.
// Does NOT touch pull resistors or drive strength — call the dedicated
// setters for those.
void hal_gpio_init(hal_gpio_pin_t pin, hal_gpio_dir_t dir);

// Drive an output pin high (true) or low (false). Undefined if the pin
// is configured as input; the fake records the call regardless.
void hal_gpio_put(hal_gpio_pin_t pin, bool value);

// Return the current logical level of the pin. Works for both input
// and output pins (reads the output latch if output).
bool hal_gpio_get(hal_gpio_pin_t pin);

// Enable/disable internal pull resistors independently.
//   pull_up = true, pull_down = false  — pulled to Vdd
//   pull_up = false, pull_down = true  — pulled to GND
//   both false                         — floating
//   both true                          — platform-defined (RP2040: keeper)
void hal_gpio_set_pulls(hal_gpio_pin_t pin, bool pull_up, bool pull_down);

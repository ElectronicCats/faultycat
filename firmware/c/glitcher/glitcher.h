#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"

void glitcher_init();
void glitcher_test_configure();
bool glitcher_simple_setup();
void glitch_test(int glitch_pin, int glitch_width);
void glitcher_loop();
bool glitcher_simple_run(uint delay, uint pulse_width, int trigger_pin, int glitch_pin);

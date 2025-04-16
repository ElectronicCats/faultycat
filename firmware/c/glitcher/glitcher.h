#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"

void glitcher_test_configure();
bool glitcher_simple_setup();
void glitcher_loop();
bool glitcher_simple_run(uint delay, uint pulse_width, int trigger_pin, int glitch_pin);

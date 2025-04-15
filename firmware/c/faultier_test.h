#pragma once

#include "faultier/glitcher/delay_compiler.h"
#include "faultier/glitcher/ft_pio.h"
#include "faultier/glitcher/glitch_compiler.h"
#include "faultier/glitcher/power_cycler.h"
#include "faultier/glitcher/trigger_compiler.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"

bool simple_glitch_run(uint delay, uint pulse_width, int trigger_pin, int glitch_pin);

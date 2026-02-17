#pragma once

#include "glitcher.h"

void glitcher_commands_configure();

void glitcher_commands_get_config();

// Command constants
static const uint8_t cmd_config_pulse_delay_cycles = 0x13;
static const uint8_t cmd_config_pulse_time_cycles = 0x14;
static const uint8_t cmd_config_trigger_type = 0x15;

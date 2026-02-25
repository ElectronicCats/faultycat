#pragma once

// FIFO commands
#define SERIAL_CMD_arm 0
#define SERIAL_CMD_disarm 1
#define SERIAL_CMD_pulse 2
#define SERIAL_CMD_status 3
#define SERIAL_CMD_enable_timeout 4
#define SERIAL_CMD_disable_timeout 5
#define SERIAL_CMD_fast_trigger 6
#define SERIAL_CMD_internal_hvp 7
#define SERIAL_CMD_external_hvp 8
#define SERIAL_CMD_config_pulse_time 9
#define SERIAL_CMD_config_pulse_power 10
#define SERIAL_CMD_toggle_gp_all 11
#define SERIAL_CMD_config_pulse_delay_cycles 12
#define SERIAL_CMD_config_pulse_time_cycles 13
#define SERIAL_CMD_config_trigger_type 14
#define SERIAL_CMD_config_serial_baud 15
#define SERIAL_CMD_config_serial_pin 16
#define SERIAL_CMD_glitch 17
#define SERIAL_CMD_config_glitch_output 18
#define SERIAL_CMD_config_trigger_pull 19

#define return_ok 0
#define return_failed 1

void serial_console();

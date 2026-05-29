#pragma once

#include <stdint.h>

// drivers/target_monitor — GP29 analog input, ADC channel 3.
//
// v2.1+ exposes an analog line tied to the target so firmware can
// observe target state during a glitch campaign. The driver gives a
// raw 12-bit sample; converting to an absolute target voltage needs
// to know the board's input divider (if any) and belongs to the
// service layer (F4+), not here.

void target_monitor_init(void);

// Raw 12-bit ADC sample (0..4095). What this maps to in volts is
// deliberately left to the service — the PCB may put a divider in
// front of GP29 to support target voltages above 3.3 V.
uint16_t target_monitor_read_raw(void);

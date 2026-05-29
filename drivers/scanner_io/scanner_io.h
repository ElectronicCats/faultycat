#pragma once

#include <stdbool.h>
#include <stdint.h>

// drivers/scanner_io — 8 channels on the v2.x scanner header
// (GP0..GP7 on Conn_01x10). Addressed by channel index 0..7 instead
// of raw GPIO numbers so the blueTag-derived pinout-scanner service
// in F8 can iterate `for ch in 0..N-1` without caring about the
// physical pin.
//
// Scope in F2a: initialize as inputs with pullup, expose direction
// and level control, and bulk-read all channels. Full bitbang for
// JTAG/SWD lives in the scanner service (F8) which uses PIO for
// throughput, not this driver.

#define SCANNER_IO_CHANNEL_COUNT 8u

typedef enum {
    SCANNER_IO_DIR_IN  = 0,
    SCANNER_IO_DIR_OUT = 1,
} scanner_io_dir_t;

// Configure every channel as input with an internal pullup. Default
// state before any target is attached — reads as all-1s.
void scanner_io_init(void);

// Change one channel's direction. Pullup state is preserved across
// direction changes on RP2040.
void scanner_io_set_dir(uint8_t channel, scanner_io_dir_t dir);

// Drive an output channel. Undefined for an input channel (the fake
// records the call; the driver does not force a direction switch on
// the caller's behalf — that would hide bugs).
void scanner_io_put(uint8_t channel, bool value);

// Read a channel's current pin level.
bool scanner_io_get(uint8_t channel);

// Read all 8 channels at once, LSB = channel 0. Handy for the diag
// snapshot in the app layer; services will usually want per-channel
// reads because timing matters.
uint8_t scanner_io_read_all(void);

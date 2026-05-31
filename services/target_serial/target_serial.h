#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_v2.h"

// services/target_serial — transparent host<->target serial bridge
// (Increment 1). CDC3 carries raw bytes; this service owns the PIO
// UART and the swd_bus_lock acquisition. The byte-shoveling between
// CDC3 and these primitives lives in apps/faultycat_fw/main.c
// (pump_target_cdc) so this module stays free of TinyUSB and host-
// testable.

#define TARGET_SERIAL_TX_GP_DEFAULT BOARD_GP_SCANNER_CH4 // GP4
#define TARGET_SERIAL_RX_GP_DEFAULT BOARD_GP_SCANNER_CH5 // GP5
#define TARGET_SERIAL_BAUD_DEFAULT  115200u

typedef enum {
    TARGET_SERIAL_DISABLED = 0,
    TARGET_SERIAL_ENABLED  = 1,
} target_serial_state_t;

typedef struct {
    target_serial_state_t state;
    uint8_t tx_gp;
    uint8_t rx_gp;
    uint32_t baud;
} target_serial_status_t;

// sysclk / (baud * 8), rounded to nearest, clamped to 1..0xFFFF.
uint32_t target_serial_baud_to_divider(uint32_t baud);

// Reset to DISABLED with default pins/baud. No hardware touched.
void target_serial_init(void);

// Acquire the bus, configure + enable the PIO UART. Fails if already
// enabled, pins out of the scanner range (0..7) or equal, baud is 0,
// or the bus is held by another owner.
bool target_serial_enable(uint8_t tx_gp, uint8_t rx_gp, uint32_t baud);

// Live baud change. Returns false unless currently ENABLED and baud>0.
bool target_serial_set_baud(uint32_t baud);

// Disable + release the bus. No-op if already DISABLED.
void target_serial_disable(void);

void target_serial_get_status(target_serial_status_t* out);

// Byte primitives — no-ops returning false/0 while DISABLED.
bool target_serial_tx_byte(uint8_t b);
size_t target_serial_rx_drain(uint8_t* buf, size_t cap);

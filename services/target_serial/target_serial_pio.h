#pragma once

#include <stdbool.h>
#include <stdint.h>

// services/target_serial/target_serial_pio — PIO UART physical layer.
//
// Claims pio1/SM1 (UART TX) + pio1/SM2 (UART RX). pio1/SM0 is swd_phy;
// pio1/SM3 is deliberately left free for the Increment-2 passive
// sniffer's second RX. The two state machines run at 8x the UART baud
// (8 PIO cycles per bit), so the divider is sysclk / (baud * 8).
//
// Programs are the canonical raspberrypi/pico-examples UART programs
// (BSD-3), hand-encoded inline (this tree has no pioasm step). The RX
// program uses an explicit `push` instead of the upstream
// uart_rx_mini autopush, because hal_pio_sm_cfg_t does not expose the
// autopush threshold.

// Claim SM1+SM2, load both programs, route pins, configure dividers,
// enable. Returns false if a SM is already claimed, the PIO has no
// program room, or the instance is unavailable.
bool target_serial_pio_init(uint8_t tx_gp, uint8_t rx_gp, uint32_t divider);

// Stop + unclaim both SMs, remove both programs, restore the two pins
// to plain GPIO inputs so scanner_io / swd_phy can re-claim them.
// Safe to call when not inited.
void target_serial_pio_deinit(void);

// Reprogram the integer clock divider on both SMs (live baud change).
void target_serial_pio_set_divider(uint32_t divider);

// Non-blocking TX: queue one byte to the TX FIFO. Returns false if not
// inited or the FIFO is full.
bool target_serial_pio_try_put(uint8_t b);

// Non-blocking RX: pop one received byte. Returns false if not inited
// or the RX FIFO is empty.
bool target_serial_pio_try_get(uint8_t* out);

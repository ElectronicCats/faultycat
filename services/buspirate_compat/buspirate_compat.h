#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// services/buspirate_compat — BusPirate v1 binary mode + OpenOCD JTAG
// sub-mode, streaming flavour (no 4 KB buffer per CMD_TAP_SHIFT).
//
// What this implements: enough of the Dangerous Prototypes BusPirate
// binary protocol to drive an OpenOCD JTAG session through FaultyCat
// over CDC2. Specifically the path OpenOCD's `interface/buspirate.cfg`
// driver walks during a typical `init; halt; exit`:
//
//   Host →  0x00 (×N until reply)
//   Dev  →  "BBIO1"
//   Host →  0x06            (enter OpenOCD mode)
//   Dev  →  "OCD1"
//   Host →  0x05 nh nl      (CMD_TAP_SHIFT, big-endian bit count)
//   Host →  TDI / TMS bytes interleaved, ceil(N/8)*2 of them
//   Dev  →  0x05 nh nl + TDO byte per pair
//   ... repeat TAP_SHIFT ...
//   Host →  0x0F            (reset back to user terminal)
//   Dev  →  invokes on_exit, the shell goes back to text mode.
//
// Algorithm shape adapted from blueTag @ v2.1.2's
// `src/modules/openocd/openocdJTAG.c` (MIT, attribution at file head
// of buspirate_compat.c) — but the upstream uses blocking getc(stdin)
// and a 4 KB stage buffer per TAP_SHIFT. We're cooperative (TinyUSB,
// EMFI campaign, crowbar campaign all share the main loop), so the
// state machine here consumes one byte at a time and processes each
// (TDI,TMS) pair the moment both arrive — output streams byte-by-byte
// alongside input.
//
// Architecture: callback-based so the test build can stub out both
// the byte-output sink and the JTAG bit clock (no need to wire the
// hal_fake_pio FIFO). The shell wires `write_byte` →
// usb_composite_cdc_write(USB_CDC_SCANNER, ...), `jtag_clock_bit` →
// jtag_core's primitive, and `on_exit` to the shell mode flag +
// jtag_deinit.

typedef struct {
    void (*write_byte)(uint8_t b, void *user);
    bool (*jtag_clock_bit)(bool tms, bool tdi, void *user);
    void (*on_exit)(void *user);
    void *user;
} buspirate_compat_callbacks_t;

// Reset state to BBIO mode and stash the callback table. The state
// machine starts in BBIO since that's what OpenOCD assumes when it
// opens the port (it sends 0x00 ×25 expecting "BBIO1").
void buspirate_compat_init(const buspirate_compat_callbacks_t *cb);

// Feed one byte from the host. Internally invokes `write_byte` zero
// or more times (for replies) and `jtag_clock_bit` zero or more
// times (per CMD_TAP_SHIFT bit). On 0x0F (reset to user terminal)
// invokes `on_exit` exactly once and resets back to BBIO state.
void buspirate_compat_feed_byte(uint8_t b);

// Visible state for diagnostic / tests. BBIO_IDLE = expecting an
// entry-mode byte (0x00, 0x06, 0x0F, …); OCD_IDLE = inside OOCD
// awaiting a sub-command; the rest are mid-command.
typedef enum {
    BUSPIRATE_BBIO_IDLE        = 0,
    BUSPIRATE_OCD_IDLE         = 1,
    BUSPIRATE_OCD_PORT_MODE_1  = 2,   // CMD_PORT_MODE — consume 1 arg byte
    BUSPIRATE_OCD_FEATURE_1    = 3,   // CMD_FEATURE — consume 2 arg bytes
    BUSPIRATE_OCD_FEATURE_2    = 4,
    BUSPIRATE_OCD_TAP_LEN_HI   = 5,   // CMD_TAP_SHIFT — 2-byte big-endian length
    BUSPIRATE_OCD_TAP_LEN_LO   = 6,
    BUSPIRATE_OCD_TAP_TDI      = 7,   // streaming TDI/TMS pair bytes
    BUSPIRATE_OCD_TAP_TMS      = 8,
    BUSPIRATE_OCD_UART_SPEED_1 = 9,   // CMD_UART_SPEED — consume 3 arg bytes
    BUSPIRATE_OCD_UART_SPEED_2 = 10,
    BUSPIRATE_OCD_UART_SPEED_3 = 11,
    BUSPIRATE_OCD_JTAG_SPEED_1 = 12,  // CMD_JTAG_SPEED — consume 2 arg bytes
    BUSPIRATE_OCD_JTAG_SPEED_2 = 13,
} buspirate_compat_state_t;

buspirate_compat_state_t buspirate_compat_get_state(void);

// Maximum CMD_TAP_SHIFT bit count we accept. Matches blueTag's clamp.
#define BUSPIRATE_TAP_SHIFT_MAX_BITS 0x2000u

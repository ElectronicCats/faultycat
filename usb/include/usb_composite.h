#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// usb/ — FaultyCat v3 USB composite device.
//
// F3-1: 4× CDC (emfi, crowbar, scanner, target-uart).
// F3-2: + Vendor IF "CMSIS-DAP v2" (stub).
// F3-3: + HID IF  "CMSIS-DAP v1" (stub).
// F3-4: migrate diag to CDC2 (scanner).

// CDC indices — keep in sync with the descriptor tables in
// usb_descriptors.c. The composite exposes the four CDCs in this
// fixed order; services consume them by index.
typedef enum {
    USB_CDC_EMFI    = 0,  // CDC0 → glitch_engine/emfi (F4)
    USB_CDC_CROWBAR = 1,  // CDC1 → glitch_engine/crowbar (F5)
    USB_CDC_SCANNER = 2,  // CDC2 → pinout_scanner shell + diag (F8/F3-4)
    USB_CDC_TARGET  = 3,  // CDC3 → target-uart PIO passthrough (F8)
    USB_CDC_COUNT   = 4,
} usb_cdc_index_t;

// Initialize the USB stack. Must be called once before the main
// cooperative loop starts. Calls tusb_init() and sets the USB
// peripheral into device mode.
void usb_composite_init(void);

// Pump TinyUSB + every per-CDC worker. Must be called often from
// the main loop (the legacy firmware called it from the
// multicore-FIFO wait path; v3 calls it from the diag loop at
// ~50 Hz at least).
void usb_composite_task(void);

// Is the host currently connected to CDC `idx` (DTR asserted)?
bool usb_composite_cdc_connected(usb_cdc_index_t idx);

// Write bytes to CDC `idx`. Returns the number of bytes actually
// queued. Non-blocking; if the TX FIFO is full the remainder is
// dropped — callers that care must retry or aggregate upstream.
size_t usb_composite_cdc_write(usb_cdc_index_t idx,
                               const void *data, size_t len);

// Convenience wrapper for NUL-terminated strings.
size_t usb_composite_cdc_write_str(usb_cdc_index_t idx, const char *s);

// Read up to `cap` bytes from CDC `idx` into `data`. Non-blocking;
// returns the count actually read (0 if nothing available).
size_t usb_composite_cdc_read(usb_cdc_index_t idx, void *data, size_t cap);

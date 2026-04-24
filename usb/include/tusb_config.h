#pragma once

// TinyUSB configuration for the FaultyCat v3 composite.
// 4× CDC + (F3-2) Vendor CMSIS-DAP v2 + (F3-3) HID CMSIS-DAP v1.
// F3-1 brings up only the CDCs; vendor + HID flip on in later commits.

#define CFG_TUSB_MCU                OPT_MCU_RP2040
#define CFG_TUSB_OS                 OPT_OS_PICO
#define CFG_TUSB_DEBUG              0

// TinyUSB 0.18.0 (pico-sdk 2.1.1) expects the legacy rhport-mode
// define, even though it also provides the newer CFG_TUD_ENABLED /
// CFG_TUD_MAX_SPEED pair. Define both for forward/backward compat.
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE      64

// Device classes
#define CFG_TUD_CDC                 4
#define CFG_TUD_VENDOR              0    // F3-2 flips this to 1
#define CFG_TUD_HID                 0    // F3-3 flips this to 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_MIDI                0

// CDC FIFO sizes — 256 B in each direction per endpoint keeps the
// buffers tight for a full-speed device while leaving room for the
// binary protocols in F4+.
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      256

// Endpoint max packet sizes (bulk on CDC data)
#define CFG_TUD_CDC_EP_BUFSIZE      64

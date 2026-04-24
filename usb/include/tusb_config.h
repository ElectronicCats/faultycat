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
#define CFG_TUD_VENDOR              1    // F3-2: CMSIS-DAP v2 stub
#define CFG_TUD_HID                 1    // F3-3: CMSIS-DAP v1 stub
#define CFG_TUD_MSC                 0
#define CFG_TUD_MIDI                0

// HID — one interrupt IN endpoint, 64 B report on the way out, and
// we poll tud_hid_n_receive_cb for input reports. CMSIS-DAP v1
// convention.
#define CFG_TUD_HID_EP_BUFSIZE      64

// CDC FIFO sizes. TX is 1024 so the diag banner (a burst of ~10
// printfs on connect) doesn't truncate before tud_task drains it.
// RX stays at 256 — F8's scanner shell will bump it per-command.
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      1024

// Endpoint max packet sizes (bulk on CDC data)
#define CFG_TUD_CDC_EP_BUFSIZE      64

// Vendor FIFO sizes — CMSIS-DAP v2 max packet 64 bytes; 256 gives
// headroom for queuing.
#define CFG_TUD_VENDOR_RX_BUFSIZE   256
#define CFG_TUD_VENDOR_TX_BUFSIZE   256

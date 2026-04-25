#include "usb_composite.h"

#include <string.h>

#include "dap_stub.h"
#include "pico/bootrom.h"
#include "tusb.h"

// Magic baud rate — same convention pico-sdk's stdio_usb uses. Any
// host that opens any of our CDCs at 1200 baud is asking us to drop
// to BOOTSEL so the next flash doesn't need the physical button.
// Matches what `picotool reboot -f -u` does on pico-sdk firmware.
#define USB_MAGIC_BOOTSEL_BAUD 1200u

// TinyUSB callback: invoked when the host sets line coding on CDC
// `itf`. If the host asks for the magic baud, reboot into the
// RP2040 bootrom with USB mass-storage mode enabled so the next
// `cp faultycat.uf2 /media/RPI-RP2/` lands.
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *coding) {
    (void)itf;
    if (coding != NULL && coding->bit_rate == USB_MAGIC_BOOTSEL_BAUD) {
        // `reset_usb_boot(0, 0)` — no activity LED pin, no disable
        // flags: drop to mass-storage BOOTSEL as if the physical
        // button had been pressed at boot.
        reset_usb_boot(0, 0);
    }
}


void usb_composite_init(void) {
    tusb_init();
}

// Echo every received byte back on the same CDC. F3-1 exists to
// prove enumeration + bidirectional traffic per interface; later
// commits replace this per-CDC with real protocol handlers
// (emfi_proto, crowbar_proto, scanner shell, UART passthrough).
static void echo_cdc(uint8_t cdc_idx) {
    if (!tud_cdc_n_available(cdc_idx)) {
        return;
    }
    uint8_t buf[64];
    uint32_t count = tud_cdc_n_read(cdc_idx, buf, sizeof(buf));
    if (count == 0) {
        return;
    }
    tud_cdc_n_write(cdc_idx, buf, count);
    tud_cdc_n_write_flush(cdc_idx);
}

// CMSIS-DAP v2 stub: pull a request packet off the vendor IF, hand
// it to dap_stub_handle, push the response back. F7 replaces this
// with services/daplink_usb/ which streams SWD transfers and talks
// to the real glitch-engine mutex.
static void pump_vendor(void) {
    uint32_t avail = tud_vendor_available();
    if (avail == 0u) {
        return;
    }
    uint8_t req[64];
    uint32_t n = tud_vendor_read(req, sizeof(req));
    if (n == 0u) {
        return;
    }

    uint8_t resp[64];
    size_t resp_len = dap_stub_handle(req, (size_t)n, resp, sizeof(resp));
    if (resp_len == 0u) {
        return;
    }
    tud_vendor_write(resp, (uint32_t)resp_len);
    tud_vendor_write_flush();
}

void usb_composite_task(void) {
    tud_task();
    // CDC0 owned by emfi_proto (F4), CDC1 by crowbar_proto (F5-4),
    // CDC2 by the swd_shell (F6-5) — all pumped from main.c. CDC3
    // (target-UART) still echoes until F8 claims it. Reading any
    // owned CDC here would race the main-side pump and consume the
    // bytes before the protocol/parser sees them (the F5-4 echo
    // bug, repeated at F6-5 — see memory note feedback_usb_cdc_
    // echo_loop).
    for (uint8_t i = 3; i < USB_CDC_COUNT; i++) {
        echo_cdc(i);
    }
    pump_vendor();
    // HID pump is entirely callback-driven (tud_hid_set_report_cb
    // below); nothing to poll from the main loop.
}

// ---------------------------------------------------------------------------
// HID CMSIS-DAP v1 stub (F3-3)
// ---------------------------------------------------------------------------
// The v1 transport wraps the same DAP command bytes as v2 in HID
// reports. Host writes a 64 B output report with the command, we
// process it through dap_stub_handle (shared with v2), and ship the
// response back as a 64 B input report via tud_hid_report().

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;

    // HID_REPORT_TYPE_INVALID is what TinyUSB reports for
    // interrupt-out (no SET_REPORT control transfer involved);
    // HID_REPORT_TYPE_OUTPUT is what SET_REPORT sends. Accept both —
    // CMSIS-DAP hosts will pick one or the other.
    if (report_type != HID_REPORT_TYPE_OUTPUT
     && report_type != HID_REPORT_TYPE_INVALID) {
        return;
    }

    uint8_t resp[CFG_TUD_HID_EP_BUFSIZE];
    size_t resp_len = dap_stub_handle(buffer, (size_t)bufsize,
                                      resp, sizeof(resp));
    if (resp_len == 0u) {
        return;
    }
    tud_hid_report(0, resp, (uint16_t)resp_len);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    // Unsolicited GET_REPORT is not used by CMSIS-DAP v1 hosts;
    // responses come back via tud_hid_report from set_report_cb.
    // Returning 0 signals "no report to send".
    return 0;
}

bool usb_composite_cdc_connected(usb_cdc_index_t idx) {
    if ((unsigned)idx >= USB_CDC_COUNT) {
        return false;
    }
    return tud_cdc_n_connected((uint8_t)idx);
}

size_t usb_composite_cdc_write(usb_cdc_index_t idx,
                               const void *data, size_t len) {
    if ((unsigned)idx >= USB_CDC_COUNT || data == NULL || len == 0) {
        return 0;
    }
    uint32_t written = tud_cdc_n_write((uint8_t)idx, data, (uint32_t)len);
    tud_cdc_n_write_flush((uint8_t)idx);
    return (size_t)written;
}

size_t usb_composite_cdc_write_str(usb_cdc_index_t idx, const char *s) {
    if (s == NULL) {
        return 0;
    }
    return usb_composite_cdc_write(idx, s, strlen(s));
}

size_t usb_composite_cdc_read(usb_cdc_index_t idx, void *data, size_t cap) {
    if ((unsigned)idx >= USB_CDC_COUNT || data == NULL || cap == 0) return 0;
    if (!tud_cdc_n_available((uint8_t)idx)) return 0;
    uint32_t n = tud_cdc_n_read((uint8_t)idx, data, (uint32_t)cap);
    return (size_t)n;
}

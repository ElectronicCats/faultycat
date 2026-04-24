#include "usb_composite.h"

#include <string.h>

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
    for (uint8_t i = 0; i < USB_CDC_COUNT; i++) {
        echo_cdc(i);
    }
    pump_vendor();
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

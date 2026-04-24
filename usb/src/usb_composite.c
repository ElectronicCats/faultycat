#include "usb_composite.h"

#include <string.h>

#include "tusb.h"

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

void usb_composite_task(void) {
    tud_task();
    for (uint8_t i = 0; i < USB_CDC_COUNT; i++) {
        echo_cdc(i);
    }
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

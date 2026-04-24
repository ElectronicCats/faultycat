#include "dap_stub.h"

#include <stdio.h>
#include <string.h>

#include "pico/unique_id.h"

// CMSIS-DAP command IDs (only the ones we handle or reject explicitly)
#define DAP_INFO        0x00
#define DAP_ERROR       0xFF

// DAP_Info subcodes (ARM CMSIS-DAP spec, §Commands → DAP_Info)
#define DAP_INFO_VENDOR_NAME         0x01
#define DAP_INFO_PRODUCT_NAME        0x02
#define DAP_INFO_SERIAL_NUMBER       0x03
#define DAP_INFO_CMSIS_DAP_FW_VER    0x04
#define DAP_INFO_PRODUCT_FW_VER      0x09
#define DAP_INFO_CAPABILITIES        0xF0
#define DAP_INFO_PACKET_COUNT        0xFE
#define DAP_INFO_PACKET_SIZE         0xFF

// Strings we report. Match the USB descriptor strings so a DAP
// client cross-checking against the USB descriptor sees them agree.
#define DAP_VENDOR_NAME    "Electronic Cats"
#define DAP_PRODUCT_NAME   "FaultyCat v3"
#define DAP_CMSIS_DAP_VER  "2.1.0"        // ARM spec version we target
#define DAP_PRODUCT_FW_VER "3.0.0-f3-2"   // our firmware version tag

// Build a CMSIS-DAP "string" response: byte0 = echo of cmd,
// byte1 = length-in-bytes-including-null-terminator, byte2+ = ASCII + '\0'.
static size_t emit_string(uint8_t *resp, size_t cap, const char *s) {
    size_t len = strlen(s) + 1u;
    if (cap < 2 + len) {
        // Not enough room — report empty and carry on.
        resp[1] = 0;
        return 2;
    }
    resp[1] = (uint8_t)len;
    memcpy(&resp[2], s, len);
    return 2 + len;
}

// Build the serial string from the RP2040's unique flash ID, same
// shape as the USB descriptor string descriptor.
static void build_serial(char *dst, size_t dst_cap) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);

    static const char hex[] = "0123456789ABCDEF";
    size_t k = 0;
    const char *prefix = "FLT3-";
    while (*prefix && k + 1 < dst_cap) {
        dst[k++] = *prefix++;
    }
    for (size_t i = 0; i < sizeof(uid.id) && k + 2 < dst_cap; i++) {
        dst[k++] = hex[(uid.id[i] >> 4) & 0xF];
        dst[k++] = hex[uid.id[i] & 0xF];
    }
    dst[k] = '\0';
}

size_t dap_stub_handle(const uint8_t *req, size_t req_len,
                       uint8_t *resp, size_t resp_capacity) {
    if (req == NULL || resp == NULL || req_len == 0 || resp_capacity < 2) {
        return 0;
    }
    uint8_t cmd = req[0];
    resp[0] = cmd;

    if (cmd == DAP_INFO && req_len >= 2) {
        uint8_t info = req[1];
        switch (info) {
            case DAP_INFO_VENDOR_NAME:
                return emit_string(resp, resp_capacity, DAP_VENDOR_NAME);

            case DAP_INFO_PRODUCT_NAME:
                return emit_string(resp, resp_capacity, DAP_PRODUCT_NAME);

            case DAP_INFO_SERIAL_NUMBER: {
                char serial[32];
                build_serial(serial, sizeof(serial));
                return emit_string(resp, resp_capacity, serial);
            }

            case DAP_INFO_CMSIS_DAP_FW_VER:
                return emit_string(resp, resp_capacity, DAP_CMSIS_DAP_VER);

            case DAP_INFO_PRODUCT_FW_VER:
                return emit_string(resp, resp_capacity, DAP_PRODUCT_FW_VER);

            case DAP_INFO_CAPABILITIES:
                // Byte response: bit 0 = SWD, bit 1 = JTAG, bit 2 = SWO-UART, …
                // F3-2 is a stub — no SWD/JTAG yet. F7 lights these up.
                resp[1] = 1;   // length
                resp[2] = 0x00;
                return 3;

            case DAP_INFO_PACKET_COUNT:
                // Byte response: 1 packet in flight at a time.
                resp[1] = 1;
                resp[2] = 1;
                return 3;

            case DAP_INFO_PACKET_SIZE:
                // Word (LE): max 64 bytes per bulk transfer.
                resp[1] = 2;
                resp[2] = 0x40;
                resp[3] = 0x00;
                return 4;

            default:
                // Known-but-unsupported info subcode: report length 0.
                resp[1] = 0;
                return 2;
        }
    }

    // Unknown command — signal error. Hosts treat 0xFF in the status
    // byte as "request rejected"; they fall through to safer paths
    // instead of retrying forever.
    resp[1] = DAP_ERROR;
    return 2;
}

// FaultyCat v3 — USB descriptors for the composite device.
//
// F3-1: 4 CDC (emfi, crowbar, scanner, target-uart). No vendor/HID
// yet — those land in F3-2 and F3-3 inside the SAME endpoint budget
// (16/16). The CDC endpoint numbers below are chosen with that
// headroom reserved: CDC0..CDC3 use EPs 0x01..0x05 for data OUT and
// 0x81..0x88 for notification + data IN, leaving 0x06/0x86 free for
// the F3-2 vendor IF and 0x87 for the F3-3 HID IF.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "tusb.h"
#include "pico/unique_id.h"

#include "usb_composite.h"

// ---------------------------------------------------------------------------
// Device descriptor
// ---------------------------------------------------------------------------

#ifndef FAULTYCAT_USB_VID
#define FAULTYCAT_USB_VID 0x1209       // pid.codes community
#endif
#ifndef FAULTYCAT_USB_PID
#define FAULTYCAT_USB_PID 0xFA17       // dev allocation for this project
#endif
#ifndef FAULTYCAT_USB_BCD
#define FAULTYCAT_USB_BCD 0x0300       // 3.00 — v3 firmware
#endif

static const tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    // bcdUSB 2.01 advertises BOS descriptor support so Windows
    // queries the MS OS 2.0 descriptor set we provide.
    .bcdUSB             = 0x0201,
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = FAULTYCAT_USB_VID,
    .idProduct          = FAULTYCAT_USB_PID,
    .bcdDevice          = FAULTYCAT_USB_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

// TinyUSB callback — return the device descriptor as a byte array.
uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&s_device_desc;
}

// ---------------------------------------------------------------------------
// Configuration descriptor
// ---------------------------------------------------------------------------

// Interface numbering — keep in sync with usb_composite.h
enum {
    ITF_CDC0_NOTIF = 0, ITF_CDC0_DATA,
    ITF_CDC1_NOTIF,     ITF_CDC1_DATA,
    ITF_CDC2_NOTIF,     ITF_CDC2_DATA,
    ITF_CDC3_NOTIF,     ITF_CDC3_DATA,
    ITF_VENDOR,         // F3-2 — CMSIS-DAP v2 stub
    ITF_HID,            // F3-3 — CMSIS-DAP v1 stub
    ITF_TOTAL
};

// Endpoint addresses. TinyUSB distinguishes IN (0x80 | n) and OUT (n).
// CDC assignment:
//   CDC0  notif 0x81   data in 0x82   data out 0x02
//   CDC1  notif 0x83   data in 0x84   data out 0x03
//   CDC2  notif 0x85   data in 0x86   data out 0x04
//   CDC3  notif 0x87   data in 0x88   data out 0x05
//
// Reserved for later commits of F3:
//   Vendor (F3-2) : bulk in 0x89   bulk out 0x06
//   HID    (F3-3) : int   in 0x8A                (only 1 endpoint)
//
// Note: the RP2040 has 16 physical endpoints counting IN and OUT
// halves of EP0 once. The arithmetic in plan §4 is the same.
#define EP_CDC0_NOTIF     0x81
#define EP_CDC0_DATA_IN   0x82
#define EP_CDC0_DATA_OUT  0x02
#define EP_CDC1_NOTIF     0x83
#define EP_CDC1_DATA_IN   0x84
#define EP_CDC1_DATA_OUT  0x03
#define EP_CDC2_NOTIF     0x85
#define EP_CDC2_DATA_IN   0x86
#define EP_CDC2_DATA_OUT  0x04
#define EP_CDC3_NOTIF     0x87
#define EP_CDC3_DATA_IN   0x88
#define EP_CDC3_DATA_OUT  0x05
#define EP_VENDOR_DATA_IN  0x89
#define EP_VENDOR_DATA_OUT 0x06
#define EP_HID_IN          0x8A        // single interrupt IN endpoint

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN \
                          + 4 * TUD_CDC_DESC_LEN \
                          + TUD_VENDOR_DESC_LEN \
                          + TUD_HID_DESC_LEN)

// String indices for per-interface labels. TinyUSB uses these
// indirectly via the CDC IAD descriptor macro.
enum {
    STRID_LANGID        = 0,
    STRID_MANUFACTURER  = 1,
    STRID_PRODUCT       = 2,
    STRID_SERIAL        = 3,
    STRID_CDC0          = 4,
    STRID_CDC1          = 5,
    STRID_CDC2          = 6,
    STRID_CDC3          = 7,
    STRID_VENDOR        = 8,
    STRID_HID           = 9,
};

// Vendor request number used by Windows to ask for the MS OS 2.0
// descriptor set. Arbitrary but must be reflected in the BOS
// descriptor and in the control-xfer callback.
#define VENDOR_REQUEST_MICROSOFT 0x01

// HID report descriptor for CMSIS-DAP v1. Same shape every
// CMSIS-DAP v1 probe uses:
//   * one input report (host reads from us)   — 64 B
//   * one output report (host writes to us)   — 64 B
// Usage page / usage are vendor-defined; the pairing below matches
// what libusb hid-api clients (OpenOCD CMSIS-DAP v1 backend) look
// for.
static uint8_t const s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(CFG_TUD_HID_EP_BUFSIZE),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return s_hid_report_desc;
}

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(
        /* config-number */ 1,
        /* interface count */ ITF_TOTAL,
        /* string index */ 0,
        /* total length */ CONFIG_TOTAL_LEN,
        /* attributes */ TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        /* power (mA) */ 100
    ),

    TUD_CDC_DESCRIPTOR(ITF_CDC0_NOTIF, STRID_CDC0,
                       EP_CDC0_NOTIF, 8,
                       EP_CDC0_DATA_OUT, EP_CDC0_DATA_IN,
                       CFG_TUD_CDC_EP_BUFSIZE),

    TUD_CDC_DESCRIPTOR(ITF_CDC1_NOTIF, STRID_CDC1,
                       EP_CDC1_NOTIF, 8,
                       EP_CDC1_DATA_OUT, EP_CDC1_DATA_IN,
                       CFG_TUD_CDC_EP_BUFSIZE),

    TUD_CDC_DESCRIPTOR(ITF_CDC2_NOTIF, STRID_CDC2,
                       EP_CDC2_NOTIF, 8,
                       EP_CDC2_DATA_OUT, EP_CDC2_DATA_IN,
                       CFG_TUD_CDC_EP_BUFSIZE),

    TUD_CDC_DESCRIPTOR(ITF_CDC3_NOTIF, STRID_CDC3,
                       EP_CDC3_NOTIF, 8,
                       EP_CDC3_DATA_OUT, EP_CDC3_DATA_IN,
                       CFG_TUD_CDC_EP_BUFSIZE),

    // CMSIS-DAP v2 vendor interface. One bulk OUT + one bulk IN,
    // 64-byte max packet. Windows bind to WinUSB via the MS OS 2.0
    // descriptor in BOS (see below); Linux/macOS see it as a vendor
    // class and libusb / OpenOCD / probe-rs can claim it directly.
    TUD_VENDOR_DESCRIPTOR(ITF_VENDOR, STRID_VENDOR,
                          EP_VENDOR_DATA_OUT, EP_VENDOR_DATA_IN,
                          64),

    // CMSIS-DAP v1 HID interface. Single 64-byte interrupt IN
    // endpoint, ~1 ms poll interval. The v1 protocol uses HID
    // reports (host-side hidapi) as transport for the same DAP
    // command set as v2 — we route both to dap_stub_handle. Kept in
    // F3 per plan §4 to lock in the 16/16 endpoint budget early.
    TUD_HID_DESCRIPTOR(ITF_HID, STRID_HID,
                       HID_ITF_PROTOCOL_NONE,
                       sizeof(s_hid_report_desc), EP_HID_IN,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return s_config_desc;
}

// ---------------------------------------------------------------------------
// BOS + MS OS 2.0 — Windows WinUSB auto-bind for the CMSIS-DAP v2 IF.
//
// Adapted from the raspberrypi/debugprobe project. Without these
// descriptors Windows won't bind a driver to the vendor IF; OpenOCD
// and probe-rs would be stuck until the user ran Zadig.
// ---------------------------------------------------------------------------

#define MS_OS_20_DESC_LEN 0xB2
#define BOS_TOTAL_LEN     (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static uint8_t const s_bos_desc[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

static uint8_t const s_ms_os_20[] = {
    // Set header
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),
    U32_TO_U8S_LE(0x06030000),   // Windows version: Win 8.1+
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN),

    // Configuration subset header
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),
    0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),

    // Function subset header — scope to the vendor IF only
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),
    ITF_VENDOR, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),

    // Compatible ID: WINUSB
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    // Registry property: DeviceInterfaceGUIDs = {CDB3B5AD-293B-4663-AA36-1AAE46463776}
    // Same GUID the debugprobe project uses — tools (OpenOCD, probe-rs)
    // look for this specific GUID to distinguish a CMSIS-DAP v2 probe
    // from a generic WinUSB device.
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14),
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007),       // REG_MULTI_SZ
    U16_TO_U8S_LE(0x002A),
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
    'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
    'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050),
    '{', 0x00, 'C', 0x00, 'D', 0x00, 'B', 0x00, '3', 0x00, 'B', 0x00,
    '5', 0x00, 'A', 0x00, 'D', 0x00, '-', 0x00, '2', 0x00, '9', 0x00,
    '3', 0x00, 'B', 0x00, '-', 0x00, '4', 0x00, '6', 0x00, '6', 0x00,
    '3', 0x00, '-', 0x00, 'A', 0x00, 'A', 0x00, '3', 0x00, '6', 0x00,
    '-', 0x00, '1', 0x00, 'A', 0x00, 'A', 0x00, 'E', 0x00, '4', 0x00,
    '6', 0x00, '4', 0x00, '6', 0x00, '3', 0x00, '7', 0x00, '7', 0x00,
    '6', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};

TU_VERIFY_STATIC(sizeof(s_ms_os_20) == MS_OS_20_DESC_LEN, "MS OS 2.0 descriptor size mismatch");

uint8_t const *tud_descriptor_bos_cb(void) {
    return s_bos_desc;
}

// Handle the WinUSB GET_MS_OS_20_DESCRIPTOR vendor request. This is
// what Windows sends after enumeration to decide which driver to
// bind; we answer with the descriptor set above.
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                tusb_control_request_t const *request) {
    if (stage != CONTROL_STAGE_SETUP) {
        return true;
    }

    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR
     && request->bRequest == VENDOR_REQUEST_MICROSOFT
     && request->wIndex == 7) {
        // Windows asks for "descriptor set" — the length lives at
        // offset 8 inside s_ms_os_20.
        uint16_t total_len;
        memcpy(&total_len, s_ms_os_20 + 8, 2);
        return tud_control_xfer(rhport, request,
                                (void *)(uintptr_t)s_ms_os_20,
                                total_len);
    }
    return false;
}

// ---------------------------------------------------------------------------
// String descriptors
// ---------------------------------------------------------------------------

static const char *s_string_literals[] = {
    [STRID_MANUFACTURER] = "Electronic Cats",
    [STRID_PRODUCT]      = "FaultyCat v3",
    // STRID_SERIAL is generated from the RP2040's unique flash id at
    // runtime; see tud_descriptor_string_cb below.
    [STRID_CDC0]         = "FaultyCat EMFI Control",
    [STRID_CDC1]         = "FaultyCat Crowbar Control",
    [STRID_CDC2]         = "FaultyCat Scanner Shell",
    [STRID_CDC3]         = "FaultyCat Target UART",
    [STRID_VENDOR]       = "FaultyCat CMSIS-DAP v2",
    [STRID_HID]          = "FaultyCat CMSIS-DAP v1",
};

// Scratch buffer for the UTF-16 string descriptors we hand back to
// the host. Max 32 code units per string.
static uint16_t s_string_scratch[32];

// Expand an ASCII literal into the UTF-16 little-endian blob TinyUSB
// expects. Returns the byte count including the descriptor header.
static uint16_t *encode_string(const char *s) {
    size_t n = 0;
    while (s[n] != '\0' && n < (sizeof(s_string_scratch) / sizeof(s_string_scratch[0])) - 1) {
        s_string_scratch[1 + n] = (uint16_t)s[n];
        n++;
    }
    // Header: bLength (bytes including header), bDescriptorType = 3
    s_string_scratch[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2u * (uint16_t)n + 2u));
    return s_string_scratch;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    switch (index) {
        case STRID_LANGID:
            // English (US)
            s_string_scratch[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
            s_string_scratch[1] = 0x0409;
            return s_string_scratch;

        case STRID_SERIAL: {
            // Generate a serial like "FLT3-01234567890ABCDEF" from the
            // RP2040's unique ID. 8 bytes = 16 hex chars, plus the
            // "FLT3-" prefix = 21 chars. Fits in 32-unit scratch.
            pico_unique_board_id_t uid;
            pico_get_unique_board_id(&uid);
            static const char hex[] = "0123456789ABCDEF";
            char tmp[32];
            size_t k = 0;
            tmp[k++] = 'F'; tmp[k++] = 'L'; tmp[k++] = 'T';
            tmp[k++] = '3'; tmp[k++] = '-';
            for (size_t i = 0; i < sizeof(uid.id); i++) {
                tmp[k++] = hex[(uid.id[i] >> 4) & 0xF];
                tmp[k++] = hex[uid.id[i] & 0xF];
            }
            tmp[k] = '\0';
            return encode_string(tmp);
        }

        default:
            if (index < (sizeof(s_string_literals) / sizeof(s_string_literals[0]))
             && s_string_literals[index] != NULL) {
                return encode_string(s_string_literals[index]);
            }
            return NULL;
    }
}

// FaultyCat v3 — USB descriptors for the composite device.
//
// F3-1: 4 CDC (emfi, crowbar, scanner, target-uart). No vendor/HID
// yet — those land in F3-2 and F3-3 inside the SAME endpoint budget
// (16/16). The CDC endpoint numbers below are chosen with that
// headroom reserved: CDC0..CDC3 use EPs 0x01..0x05 for data OUT and
// 0x81..0x88 for notification + data IN, leaving 0x06/0x86 free for
// the F3-2 vendor IF and 0x87 for the F3-3 HID IF.

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
    .bcdUSB             = 0x0200,
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
#define EP_CDC0_NOTIF   0x81
#define EP_CDC0_DATA_IN 0x82
#define EP_CDC0_DATA_OUT 0x02
#define EP_CDC1_NOTIF   0x83
#define EP_CDC1_DATA_IN 0x84
#define EP_CDC1_DATA_OUT 0x03
#define EP_CDC2_NOTIF   0x85
#define EP_CDC2_DATA_IN 0x86
#define EP_CDC2_DATA_OUT 0x04
#define EP_CDC3_NOTIF   0x87
#define EP_CDC3_DATA_IN 0x88
#define EP_CDC3_DATA_OUT 0x05

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 4 * TUD_CDC_DESC_LEN)

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
};

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
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return s_config_desc;
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

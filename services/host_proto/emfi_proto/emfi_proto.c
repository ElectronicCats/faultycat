#include "emfi_proto.h"

#include <string.h>

#include "emfi_campaign.h"

// Parser state ---------------------------------------------------------------

typedef enum {
    S_SOF = 0,
    S_CMD,
    S_LEN_LO,
    S_LEN_HI,
    S_PAYLOAD,
    S_CRC_LO,
    S_CRC_HI,
} parse_state_t;

static parse_state_t s_state;
static uint8_t       s_cmd;
static uint16_t      s_len;
static uint16_t      s_payload_pos;
static uint8_t       s_payload[EMFI_PROTO_MAX_PAYLOAD];
static uint16_t      s_crc_recv;
static uint32_t      s_last_byte_ms;

static bool          s_frame_ready;
static uint8_t       s_frame_cmd;
static uint16_t      s_frame_len;
static uint8_t       s_frame_payload[EMFI_PROTO_MAX_PAYLOAD];

// CRC-16/CCITT ---------------------------------------------------------------

uint16_t emfi_proto_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                  : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void reset_parser(void) {
    s_state       = S_SOF;
    s_cmd         = 0;
    s_len         = 0;
    s_payload_pos = 0;
    s_crc_recv    = 0;
}

void emfi_proto_init(void) {
    reset_parser();
    s_frame_ready = false;
    s_last_byte_ms = 0;
}

bool emfi_proto_feed(uint8_t byte, uint32_t now_ms) {
    if (s_state != S_SOF
     && (now_ms - s_last_byte_ms) > EMFI_PROTO_INTERBYTE_MS) {
        reset_parser();
    }
    s_last_byte_ms = now_ms;

    switch (s_state) {
        case S_SOF:
            if (byte == EMFI_PROTO_SOF) s_state = S_CMD;
            return false;
        case S_CMD:
            s_cmd   = byte;
            s_state = S_LEN_LO;
            return false;
        case S_LEN_LO:
            s_len   = byte;
            s_state = S_LEN_HI;
            return false;
        case S_LEN_HI:
            s_len |= (uint16_t)byte << 8;
            if (s_len > EMFI_PROTO_MAX_PAYLOAD) { reset_parser(); return false; }
            s_payload_pos = 0;
            s_state = (s_len == 0) ? S_CRC_LO : S_PAYLOAD;
            return false;
        case S_PAYLOAD:
            s_payload[s_payload_pos++] = byte;
            if (s_payload_pos >= s_len) s_state = S_CRC_LO;
            return false;
        case S_CRC_LO:
            s_crc_recv = byte;
            s_state = S_CRC_HI;
            return false;
        case S_CRC_HI: {
            s_crc_recv |= (uint16_t)byte << 8;
            uint8_t hdr[3] = { s_cmd, (uint8_t)(s_len & 0xFFu),
                               (uint8_t)((s_len >> 8) & 0xFFu) };
            uint16_t calc = emfi_proto_crc16(hdr, 3);
            // Continue CRC over payload
            for (uint16_t i = 0; i < s_len; i++) {
                uint16_t crc = calc;
                crc ^= (uint16_t)s_payload[i] << 8;
                for (int b = 0; b < 8; b++) {
                    crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u)
                                          : (uint16_t)(crc << 1);
                }
                calc = crc;
            }
            bool ok = (calc == s_crc_recv);
            if (ok) {
                s_frame_cmd = s_cmd;
                s_frame_len = s_len;
                memcpy(s_frame_payload, s_payload, s_len);
                s_frame_ready = true;
            }
            reset_parser();
            return ok;
        }
    }
    return false;
}

// Writer ---------------------------------------------------------------------

static size_t write_frame(uint8_t *out, size_t cap,
                          uint8_t cmd_reply, const uint8_t *payload, uint16_t len) {
    if (len > EMFI_PROTO_MAX_PAYLOAD) return 0;
    size_t needed = 1u + 1u + 2u + (size_t)len + 2u;
    if (cap < needed) return 0;
    out[0] = EMFI_PROTO_SOF;
    out[1] = cmd_reply;
    out[2] = (uint8_t)(len & 0xFFu);
    out[3] = (uint8_t)((len >> 8) & 0xFFu);
    if (len) memcpy(&out[4], payload, len);
    uint16_t crc = emfi_proto_crc16(&out[1], 3u + (size_t)len);
    out[4 + len]     = (uint8_t)(crc & 0xFFu);
    out[4 + len + 1] = (uint8_t)((crc >> 8) & 0xFFu);
    return needed;
}

// Payload packing helpers ----------------------------------------------------

static void pack_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t unpack_u32_le(const uint8_t *p) {
    return ((uint32_t)p[0])
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint16_t unpack_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

// Dispatch -------------------------------------------------------------------

size_t emfi_proto_dispatch(uint8_t *reply, size_t reply_cap) {
    if (!s_frame_ready || !reply) return 0;
    s_frame_ready = false;

    uint8_t  rpl[64];
    uint16_t rpl_len = 0;
    uint8_t  err     = EMFI_ERR_NONE;

    switch (s_frame_cmd) {
        case EMFI_CMD_PING: {
            static const uint8_t pong[] = { 'F','4',0,0 };
            memcpy(rpl, pong, sizeof(pong));
            rpl_len = (uint16_t)sizeof(pong);
            break;
        }
        case EMFI_CMD_CONFIGURE: {
            if (s_frame_len < 1u + 4u + 4u + 4u) { err = EMFI_ERR_BAD_CONFIG; break; }
            emfi_config_t c = {
                .trigger           = (emfi_trig_t)s_frame_payload[0],
                .delay_us          = unpack_u32_le(&s_frame_payload[1]),
                .width_us          = unpack_u32_le(&s_frame_payload[5]),
                .charge_timeout_ms = unpack_u32_le(&s_frame_payload[9]),
            };
            if (!emfi_campaign_configure(&c)) err = EMFI_ERR_BAD_CONFIG;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case EMFI_CMD_ARM: {
            if (!emfi_campaign_arm()) err = EMFI_ERR_BAD_CONFIG;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case EMFI_CMD_FIRE: {
            if (s_frame_len < 4u) { err = EMFI_ERR_BAD_CONFIG; rpl[0] = err; rpl_len = 1; break; }
            uint32_t to = unpack_u32_le(s_frame_payload);
            if (!emfi_campaign_fire(to)) err = EMFI_ERR_INTERNAL;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case EMFI_CMD_DISARM: {
            emfi_campaign_disarm();
            rpl[0] = EMFI_ERR_NONE; rpl_len = 1;
            break;
        }
        case EMFI_CMD_STATUS: {
            emfi_status_t s; emfi_campaign_get_status(&s);
            rpl[0] = (uint8_t)s.state;
            rpl[1] = (uint8_t)s.err;
            pack_u32_le(&rpl[2],  s.last_fire_at_ms);
            pack_u32_le(&rpl[6],  s.capture_fill);
            pack_u32_le(&rpl[10], s.pulse_width_us_actual);
            pack_u32_le(&rpl[14], s.delay_us_actual);
            rpl_len = 18;
            break;
        }
        case EMFI_CMD_CAPTURE: {
            if (s_frame_len < 4u) { err = EMFI_ERR_BAD_CONFIG; rpl[0] = err; rpl_len = 1; break; }
            uint16_t off = unpack_u16_le(&s_frame_payload[0]);
            uint16_t len = unpack_u16_le(&s_frame_payload[2]);
            // Reject rather than clamp — host must be able to
            // distinguish "got everything requested" from a truncation.
            if (len > 512u) {
                err = EMFI_ERR_BAD_CONFIG; rpl[0] = err; rpl_len = 1; break;
            }
            if ((uint32_t)off + len > 8192u) {
                err = EMFI_ERR_BAD_CONFIG; rpl[0] = err; rpl_len = 1; break;
            }
            const uint8_t *buf = emfi_campaign_capture_buffer();
            return write_frame(reply, reply_cap,
                              (uint8_t)(s_frame_cmd | 0x80u),
                              &buf[off], len);
        }
        default:
            err = EMFI_ERR_BAD_CONFIG;
            rpl[0] = err;
            rpl_len = 1;
            break;
    }

    return write_frame(reply, reply_cap,
                      (uint8_t)(s_frame_cmd | 0x80u), rpl, rpl_len);
}

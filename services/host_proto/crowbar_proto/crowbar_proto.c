#include "crowbar_proto.h"

#include <string.h>

#include "campaign_proto.h"
#include "crowbar_campaign.h"
#include "crowbar_pio.h"

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
static uint8_t       s_payload[CROWBAR_PROTO_MAX_PAYLOAD];
static uint16_t      s_crc_recv;
static uint32_t      s_last_byte_ms;

static bool          s_frame_ready;
static uint8_t       s_frame_cmd;
static uint16_t      s_frame_len;
static uint8_t       s_frame_payload[CROWBAR_PROTO_MAX_PAYLOAD];

// CRC-16/CCITT ---------------------------------------------------------------

uint16_t crowbar_proto_crc16(const uint8_t *data, size_t len) {
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

void crowbar_proto_init(void) {
    reset_parser();
    s_frame_ready  = false;
    s_last_byte_ms = 0;
}

bool crowbar_proto_feed(uint8_t byte, uint32_t now_ms) {
    if (s_state != S_SOF
     && (now_ms - s_last_byte_ms) > CROWBAR_PROTO_INTERBYTE_MS) {
        reset_parser();
    }
    s_last_byte_ms = now_ms;

    switch (s_state) {
        case S_SOF:
            if (byte == CROWBAR_PROTO_SOF) s_state = S_CMD;
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
            if (s_len > CROWBAR_PROTO_MAX_PAYLOAD) { reset_parser(); return false; }
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
            uint16_t calc = crowbar_proto_crc16(hdr, 3);
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
    if (len > CROWBAR_PROTO_MAX_PAYLOAD) return 0;
    size_t needed = 1u + 1u + 2u + (size_t)len + 2u;
    if (cap < needed) return 0;
    out[0] = CROWBAR_PROTO_SOF;
    out[1] = cmd_reply;
    out[2] = (uint8_t)(len & 0xFFu);
    out[3] = (uint8_t)((len >> 8) & 0xFFu);
    if (len) memcpy(&out[4], payload, len);
    uint16_t crc = crowbar_proto_crc16(&out[1], 3u + (size_t)len);
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

// Dispatch -------------------------------------------------------------------
//
// Wire layout for CONFIGURE payload (10 bytes, LE):
//   [0]    trigger    (crowbar_trig_t)
//   [1]    output     (crowbar_out_t — 1=LP, 2=HP)
//   [2..5] delay_us
//   [6..9] width_ns
//
// Wire layout for STATUS reply payload (15 bytes):
//   [0]      state                  (crowbar_state_t)
//   [1]      err                    (crowbar_err_t)
//   [2..5]   last_fire_at_ms
//   [6..9]   pulse_width_ns_actual
//   [10..13] delay_us_actual
//   [14]     output                 (crowbar_out_t)

#define CONFIGURE_PAYLOAD_LEN  10u
#define STATUS_REPLY_LEN       15u
#define FIRE_PAYLOAD_LEN        4u

size_t crowbar_proto_dispatch(uint8_t *reply, size_t reply_cap) {
    if (!s_frame_ready || !reply) return 0;
    s_frame_ready = false;

    uint8_t  rpl[CROWBAR_PROTO_MAX_PAYLOAD];
    uint16_t rpl_len = 0;
    uint8_t  err     = CROWBAR_ERR_NONE;

    switch (s_frame_cmd) {
        case CROWBAR_CMD_PING: {
            static const uint8_t pong[] = { 'F','5',0,0 };
            memcpy(rpl, pong, sizeof(pong));
            rpl_len = (uint16_t)sizeof(pong);
            break;
        }
        case CROWBAR_CMD_CONFIGURE: {
            if (s_frame_len < CONFIGURE_PAYLOAD_LEN) {
                err = CROWBAR_ERR_BAD_CONFIG;
                rpl[0] = err; rpl_len = 1;
                break;
            }
            crowbar_config_t c = {
                .trigger  = (crowbar_trig_t)s_frame_payload[0],
                .output   = (crowbar_out_t)s_frame_payload[1],
                .delay_us = unpack_u32_le(&s_frame_payload[2]),
                .width_ns = unpack_u32_le(&s_frame_payload[6]),
            };
            if (!crowbar_campaign_configure(&c)) err = CROWBAR_ERR_BAD_CONFIG;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case CROWBAR_CMD_ARM: {
            if (!crowbar_campaign_arm()) err = CROWBAR_ERR_BAD_CONFIG;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case CROWBAR_CMD_FIRE: {
            if (s_frame_len < FIRE_PAYLOAD_LEN) {
                err = CROWBAR_ERR_BAD_CONFIG;
                rpl[0] = err; rpl_len = 1;
                break;
            }
            uint32_t to = unpack_u32_le(s_frame_payload);
            if (!crowbar_campaign_fire(to)) err = CROWBAR_ERR_INTERNAL;
            rpl[0] = err; rpl_len = 1;
            break;
        }
        case CROWBAR_CMD_DISARM: {
            crowbar_campaign_disarm();
            rpl[0] = CROWBAR_ERR_NONE; rpl_len = 1;
            break;
        }
        case CROWBAR_CMD_STATUS: {
            crowbar_status_t s; crowbar_campaign_get_status(&s);
            rpl[0]  = (uint8_t)s.state;
            rpl[1]  = (uint8_t)s.err;
            pack_u32_le(&rpl[2],  s.last_fire_at_ms);
            pack_u32_le(&rpl[6],  s.pulse_width_ns_actual);
            pack_u32_le(&rpl[10], s.delay_us_actual);
            rpl[14] = (uint8_t)s.output;
            rpl_len = STATUS_REPLY_LEN;
            break;
        }
        // F9-4 — campaign opcodes. Engine forced to CROWBAR since
        // we arrived on CDC1; identical wire format to emfi_proto.
        case CAMPAIGN_CMD_CONFIG: {
            campaign_config_t cfg;
            if (!campaign_proto_decode_config(s_frame_payload, s_frame_len,
                                              CAMPAIGN_ENGINE_CROWBAR, &cfg)) {
                rpl[0] = CAMPAIGN_PROTO_ERR_BAD_LEN;
            } else {
                rpl[0] = campaign_proto_apply_config(&cfg);
            }
            rpl_len = 1;
            break;
        }
        case CAMPAIGN_CMD_START: {
            rpl[0] = campaign_manager_start() ? CAMPAIGN_PROTO_OK
                                              : CAMPAIGN_PROTO_ERR_REJECTED;
            rpl_len = 1;
            break;
        }
        case CAMPAIGN_CMD_STOP: {
            campaign_manager_stop();
            rpl[0] = CAMPAIGN_PROTO_OK;
            rpl_len = 1;
            break;
        }
        case CAMPAIGN_CMD_STATUS: {
            rpl_len = (uint16_t)campaign_proto_serialize_status(rpl, sizeof(rpl));
            break;
        }
        case CAMPAIGN_CMD_DRAIN: {
            uint8_t max_count = (s_frame_len >= 1u) ? s_frame_payload[0] : 1u;
            static uint8_t drain_buf[1u + (CAMPAIGN_DRAIN_MAX_COUNT * 28u)];
            size_t drain_len = campaign_proto_serialize_drain(drain_buf,
                                                              sizeof(drain_buf),
                                                              max_count);
            return write_frame(reply, reply_cap,
                              (uint8_t)(s_frame_cmd | 0x80u),
                              drain_buf, (uint16_t)drain_len);
        }

        default:
            err = CROWBAR_ERR_BAD_CONFIG;
            rpl[0] = err;
            rpl_len = 1;
            break;
    }

    return write_frame(reply, reply_cap,
                       (uint8_t)(s_frame_cmd | 0x80u), rpl, rpl_len);
}

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// services/host_proto/crowbar_proto — binary framing for the CDC1
// ("Crowbar Control") link between host and the crowbar_campaign
// service.
//
// Frame layout mirrors emfi_proto:
//   [0]    SOF = 0xFA
//   [1]    CMD (host→device) or CMD|0x80 (device→host reply)
//   [2..3] LEN (little-endian, 0..CROWBAR_PROTO_MAX_PAYLOAD)
//   [4..]  PAYLOAD
//   [4+LEN..] CRC-16/CCITT (poly 0x1021, init 0xFFFF), LE
//
// 100 ms inter-byte timeout resets the parser.
//
// Crowbar has no large capture buffer (see crowbar_campaign for the
// rationale), so MAX_PAYLOAD is small — 64 bytes is plenty for the
// status reply and the CONFIGURE payload.

#define CROWBAR_PROTO_SOF            0xFAu
#define CROWBAR_PROTO_MAX_PAYLOAD    64u
#define CROWBAR_PROTO_INTERBYTE_MS   100u

typedef enum {
    CROWBAR_CMD_PING      = 0x01,
    CROWBAR_CMD_CONFIGURE = 0x10,
    CROWBAR_CMD_ARM       = 0x11,
    CROWBAR_CMD_FIRE      = 0x12,
    CROWBAR_CMD_DISARM    = 0x13,
    CROWBAR_CMD_STATUS    = 0x14,
} crowbar_cmd_t;

void crowbar_proto_init(void);

// Feed one byte into the parser. Returns true iff a complete,
// CRC-valid frame was just assembled; in that case call
// `crowbar_proto_dispatch` to act on it. On parse error (bad SOF,
// LEN overflow, CRC mismatch, inter-byte timeout) the state is reset.
bool crowbar_proto_feed(uint8_t byte, uint32_t now_ms);

// Act on the last-assembled frame. Writes the reply frame (including
// SOF+CMD|0x80+LEN+PAYLOAD+CRC) into `reply` and returns its length.
// Returns 0 if no reply is to be sent.
size_t crowbar_proto_dispatch(uint8_t *reply, size_t reply_cap);

// CRC-16/CCITT helper exposed for tests. Same polynomial/seed as
// emfi_proto so a future refactor could share the helper.
uint16_t crowbar_proto_crc16(const uint8_t *data, size_t len);

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// services/host_proto/emfi_proto — binary framing for the CDC0
// ("EMFI Control") link between host and the emfi_campaign service.
//
// Frame layout:
//   [0]    SOF = 0xFA
//   [1]    CMD (host→device) or CMD|0x80 (device→host reply)
//   [2..3] LEN (little-endian, 0..512)
//   [4..]  PAYLOAD
//   [4+LEN..] CRC-16/CCITT (poly 0x1021, init 0xFFFF), LE
//
// 100 ms inter-byte timeout resets the parser.

#define EMFI_PROTO_SOF            0xFAu
#define EMFI_PROTO_MAX_PAYLOAD    512u
#define EMFI_PROTO_INTERBYTE_MS   100u

typedef enum {
    EMFI_CMD_PING      = 0x01,
    EMFI_CMD_CONFIGURE = 0x10,
    EMFI_CMD_ARM       = 0x11,
    EMFI_CMD_FIRE      = 0x12,
    EMFI_CMD_DISARM    = 0x13,
    EMFI_CMD_STATUS    = 0x14,
    EMFI_CMD_CAPTURE   = 0x15,
} emfi_cmd_t;

// Initialize parser state.
void emfi_proto_init(void);

// Feed one byte into the parser. Returns true iff a complete,
// CRC-valid frame was just assembled; in that case call
// `emfi_proto_dispatch` to act on it. On parse error (bad SOF,
// LEN overflow, CRC mismatch, inter-byte timeout) the state is reset.
bool emfi_proto_feed(uint8_t byte, uint32_t now_ms);

// Act on the last-assembled frame. Writes the reply frame (including
// SOF+CMD|0x80+LEN+PAYLOAD+CRC) into `reply` and returns its length.
// Returns 0 if no reply is to be sent.
size_t emfi_proto_dispatch(uint8_t *reply, size_t reply_cap);

// CRC-16/CCITT helper exposed for tests.
uint16_t emfi_proto_crc16(const uint8_t *data, size_t len);

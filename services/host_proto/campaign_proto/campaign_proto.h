#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "campaign_manager.h"

// services/host_proto/campaign_proto — shared helpers that
// emfi_proto (CDC0) and crowbar_proto (CDC1) call to handle the
// `CAMPAIGN_*` opcode subset. Both protos use the same opcode
// numbers + payload layout; the only difference is which engine
// gets put into `cfg.engine` before forwarding to campaign_manager.
//
// The proto layers still own their own framing (SOF / CRC / LEN /
// CMD); these helpers only deal with payload semantics — config
// decode, status encode, drain encode.

// Opcode numbers (host→device commands; replies use cmd | 0x80 per
// the existing emfi_proto / crowbar_proto framing convention).
#define CAMPAIGN_CMD_CONFIG   0x20u
#define CAMPAIGN_CMD_START    0x21u
#define CAMPAIGN_CMD_STOP     0x22u
#define CAMPAIGN_CMD_STATUS   0x23u
#define CAMPAIGN_CMD_DRAIN    0x24u

// Payload sizes — fixed, validated by emfi_proto/crowbar_proto on
// the way in.
#define CAMPAIGN_CONFIG_PAYLOAD_LEN  40u  // 9×u32 axes + 1×u32 settle
#define CAMPAIGN_STATUS_REPLY_LEN    20u  // 1+1+2 hdr + 4×u32 counters
#define CAMPAIGN_DRAIN_REPLY_HDR_LEN 1u   // n_results byte
// Cap N at 18: 18 × sizeof(campaign_result_t)=28 + 1 byte header
// = 505 bytes — fits inside the 512-byte EMFI_PROTO_MAX_PAYLOAD.
// Crowbar_proto inherits the same limit. Host iterates DRAIN to
// collect more.
#define CAMPAIGN_DRAIN_MAX_COUNT     18u

// 1-byte status codes used in CONFIG / START / STOP replies.
#define CAMPAIGN_PROTO_OK            0x00u
#define CAMPAIGN_PROTO_ERR_BAD_LEN   0x01u
#define CAMPAIGN_PROTO_ERR_REJECTED  0x02u  // campaign_manager API returned false

// Decode a 40-byte CONFIG payload into a `campaign_config_t`. The
// `engine` field is set by the caller (emfi_proto vs crowbar_proto)
// since the wire format itself is engine-agnostic.
bool campaign_proto_decode_config(const uint8_t *payload, size_t len,
                                  campaign_engine_t engine,
                                  campaign_config_t *out);

// Apply a decoded config + start the campaign. Wraps the
// campaign_manager_configure → start sequence so the proto layer
// doesn't have to know the state machine. Returns CAMPAIGN_PROTO_OK
// on success or one of the ERR codes on failure.
uint8_t campaign_proto_apply_config(const campaign_config_t *cfg);

// Pack the current campaign_manager status into a 20-byte reply.
size_t campaign_proto_serialize_status(uint8_t *out, size_t cap);

// Drain up to `max_count` (capped at CAMPAIGN_DRAIN_MAX_COUNT)
// results into the reply buffer. Layout: 1-byte n then n×28-byte
// records. Returns total reply length, or 0 on cap overflow.
size_t campaign_proto_serialize_drain(uint8_t *out, size_t cap, uint8_t max_count);

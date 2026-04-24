#include "crowbar_proto.h"

// F5-1 skeleton — API-only stub. F5-4 lands the real parser, the
// dispatch table that wires into crowbar_campaign, and the CRC16
// implementation (mirror of emfi_proto).

uint16_t crowbar_proto_crc16(const uint8_t *data, size_t len) {
    (void)data;
    (void)len;
    return 0u;
}

void crowbar_proto_init(void) {
}

bool crowbar_proto_feed(uint8_t byte, uint32_t now_ms) {
    (void)byte;
    (void)now_ms;
    return false;
}

size_t crowbar_proto_dispatch(uint8_t *reply, size_t reply_cap) {
    (void)reply;
    (void)reply_cap;
    return 0u;
}

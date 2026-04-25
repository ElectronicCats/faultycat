#include "swd_dp.h"

// F6-1 skeleton — API-only stub. F6-3 lands the real wire protocol.

swd_dp_ack_t swd_dp_connect(uint32_t *out_dpidr) {
    (void)out_dpidr;
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_read_dpidr(uint32_t *out) {
    (void)out;
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_read(uint8_t addr, uint32_t *out) {
    (void)addr; (void)out;
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_write(uint8_t addr, uint32_t val) {
    (void)addr; (void)val;
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_ap_read(uint8_t bank_addr, uint32_t *out) {
    (void)bank_addr; (void)out;
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_ap_write(uint8_t bank_addr, uint32_t val) {
    (void)bank_addr; (void)val;
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_dp_abort(uint32_t flags) {
    (void)flags;
    return SWD_ACK_NO_TARGET;
}

uint8_t swd_dp_compute_parity(uint32_t v) {
    (void)v;
    return 0u;
}

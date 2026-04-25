#include "swd_mem.h"

// F6-1 skeleton — API-only stub. F6-4 lands real CSW/TAR/DRW driving.

swd_dp_ack_t swd_mem_init(void) {
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_mem_read32(uint32_t addr, uint32_t *out) {
    (void)addr; (void)out;
    return SWD_ACK_NO_TARGET;
}

swd_dp_ack_t swd_mem_write32(uint32_t addr, uint32_t val) {
    (void)addr; (void)val;
    return SWD_ACK_NO_TARGET;
}

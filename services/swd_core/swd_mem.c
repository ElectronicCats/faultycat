#include "swd_mem.h"

#include "swd_dp.h"

// MEM-AP register offsets within the active AP bank.
#define MEM_AP_CSW_OFFSET   0x00u
#define MEM_AP_TAR_OFFSET   0x04u
#define MEM_AP_DRW_OFFSET   0x0Cu

// Default CSW: 32-bit word access, AddrInc=single (TAR auto +4 per
// DRW transaction within a 1 KB region), Prot=0x23 (master,
// privileged, data) — matches openocd / CMSIS-DAP defaults for
// Cortex-M targets.
//
//   bit  31    : DbgSwEnable  (0 — most Cortex-M ignore)
//   bits 30..24: Prot         (0x23)
//   bits 5..4  : AddrInc      (01 = single increment)
//   bits 2..0  : Size         (010 = 32-bit word)
#define MEM_AP_CSW_32BIT_AUTOINC  0x23000012u

swd_dp_ack_t swd_mem_init(void) {
    // Select AP 0, bank 0 (where CSW/TAR/DRW live). The high 24 bits
    // of SELECT are reserved for future multi-AP work in F7/F9.
    swd_dp_ack_t ack = swd_dp_write(SWD_DP_ADDR_SELECT, 0x00000000u);
    if (ack != SWD_ACK_OK) return ack;
    return swd_dp_ap_write(MEM_AP_CSW_OFFSET, MEM_AP_CSW_32BIT_AUTOINC);
}

swd_dp_ack_t swd_mem_read32(uint32_t addr, uint32_t *out) {
    swd_dp_ack_t ack = swd_dp_ap_write(MEM_AP_TAR_OFFSET, addr);
    if (ack != SWD_ACK_OK) return ack;

    // Per ADIv5 the first AP read returns the result of the PREVIOUS
    // AP read; the value we want lands in DP RDBUFF. The discard of
    // *dummy is intentional — the AP transaction itself is what
    // queues the read; the bytes in `dummy` are stale and useless.
    uint32_t dummy = 0u;
    ack = swd_dp_ap_read(MEM_AP_DRW_OFFSET, &dummy);
    if (ack != SWD_ACK_OK) return ack;

    return swd_dp_read(SWD_DP_ADDR_RDBUFF, out);
}

swd_dp_ack_t swd_mem_write32(uint32_t addr, uint32_t val) {
    swd_dp_ack_t ack = swd_dp_ap_write(MEM_AP_TAR_OFFSET, addr);
    if (ack != SWD_ACK_OK) return ack;
    return swd_dp_ap_write(MEM_AP_DRW_OFFSET, val);
}

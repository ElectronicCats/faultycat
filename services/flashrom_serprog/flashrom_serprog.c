/*
 * services/flashrom_serprog/flashrom_serprog.c — Serial Flasher
 * Protocol v1 + 4-pin SPI bit-bang, streaming flavour.
 *
 * Algorithm shape adapted from third_party/blueTag @ v2.1.2's
 * `src/modules/flashProgrammer/serProg.c` (MIT). Key differences from
 * upstream:
 *
 *   1. Streaming feed_byte() instead of blocking getc(stdin) +
 *      fread(buffer, wlen). Upstream's 4 KB stack buffer is gone;
 *      we shift each write byte the moment it arrives.
 *   2. CPU bit-bang via callbacks rather than spi_write_blocking on
 *      pico-sdk's hardware_spi peripheral. Lets the operator pick
 *      any 4 scanner channels (the HW SPI mux is GP0..GP3 only).
 *   3. Tests link the .c directly with a mock callback table — no
 *      hal_fake_pio rig needed.
 *
 * --- MIT License (blueTag excerpt) -----------------------------------
 *   The MIT License (MIT)
 *   Copyright (c) 2024 Aodrulez
 *   (full text at third_party/blueTag/LICENSE +
 *    LICENSES/UPSTREAM-blueTag.txt)
 * --------------------------------------------------------------------
 */

#include "flashrom_serprog.h"

#include <string.h>

#define S_ACK                 0x06u
#define S_NAK                 0x15u

#define S_CMD_NOP             0x00u
#define S_CMD_Q_IFACE         0x01u
#define S_CMD_Q_CMDMAP        0x02u
#define S_CMD_Q_PGMNAME       0x03u
#define S_CMD_Q_SERBUF        0x04u
#define S_CMD_Q_BUSTYPE       0x05u
#define S_CMD_SYNCNOP         0x10u
#define S_CMD_S_BUSTYPE       0x12u
#define S_CMD_O_SPIOP         0x13u
#define S_CMD_S_SPI_FREQ      0x14u
#define S_CMD_S_PIN_STATE     0x15u

// Yield / progress throttle inside long SPIOP loops. 128 bytes ≈ 1
// USB FS frame's worth of work — at typical bit-bang speeds (~100
// kHz SCK) that's ~10 ms of SPI traffic, well under TinyUSB's
// patience.
#define SP_YIELD_EVERY_BYTES  128u

typedef struct {
    flashrom_serprog_callbacks_t cb;
    flashrom_serprog_state_t     state;
    uint32_t                     freq_acc;
    uint32_t                     spiop_wlen;
    uint32_t                     spiop_rlen;
    uint32_t                     spiop_done;   // bytes processed in active phase
} sp_t;

static sp_t s_sp;

// -----------------------------------------------------------------------------
// Output / SPI helpers
// -----------------------------------------------------------------------------

static void emit(uint8_t b) {
    if (s_sp.cb.write_byte) s_sp.cb.write_byte(b, s_sp.cb.user);
}

static void emit_n(const uint8_t *p, size_t n) {
    while (n--) emit(*p++);
}

static void emit_le32(uint32_t v) {
    emit((uint8_t)(v        & 0xFFu));
    emit((uint8_t)((v >> 8) & 0xFFu));
    emit((uint8_t)((v >> 16)& 0xFFu));
    emit((uint8_t)((v >> 24)& 0xFFu));
}

static uint8_t spi_xfer(uint8_t b) {
    if (s_sp.cb.spi_xfer_byte) {
        return s_sp.cb.spi_xfer_byte(b, s_sp.cb.user);
    }
    return 0xFFu;
}

static void spi_cs(bool low) {
    if (s_sp.cb.spi_cs_set) s_sp.cb.spi_cs_set(low, s_sp.cb.user);
}

static void yield_if_due(uint32_t i) {
    if (((i + 1u) % SP_YIELD_EVERY_BYTES) == 0u && s_sp.cb.yield) {
        s_sp.cb.yield(s_sp.cb.user);
    }
}

// -----------------------------------------------------------------------------
// Q_PGMNAME / Q_CMDMAP fixed payloads
// -----------------------------------------------------------------------------

// 32-byte cmdmap. Bit positions correspond to S_CMD_* constants.
//   byte 0 (cmds 0x00..0x07): NOP, Q_IFACE, Q_CMDMAP, Q_PGMNAME,
//                              Q_SERBUF, Q_BUSTYPE → bits 0..5 = 0x3F
//   byte 1 (cmds 0x08..0x0F): none = 0x00
//   byte 2 (cmds 0x10..0x17): SYNCNOP, S_BUSTYPE, O_SPIOP, S_SPI_FREQ,
//                              S_PIN_STATE → bits 0,2,3,4,5 = 0x3D
//   byte 3..31              : 0
static const uint8_t s_cmdmap[32] = {
    0x3F, 0x00, 0x3D, 0x00,
    0,0,0,0, 0,0,0,0, 0,0,0,0,
    0,0,0,0, 0,0,0,0, 0,0,0,0,
};

static void emit_pgmname(void) {
    char buf[16] = {0};
    const char *name = FLASHROM_SERPROG_PGMNAME;
    size_t n = strlen(name);
    if (n > 16u) n = 16u;
    memcpy(buf, name, n);
    emit_n((const uint8_t *)buf, 16u);
}

// -----------------------------------------------------------------------------
// SPIOP runner — invoked once both length fields are received.
// -----------------------------------------------------------------------------

static void spiop_finish_with_read_then_cs_high(void) {
    emit(S_ACK);
    for (uint32_t i = 0; i < s_sp.spiop_rlen; i++) {
        emit(spi_xfer(0u));
        yield_if_due(i);
    }
    spi_cs(false);
    s_sp.state = FLASHROM_SP_IDLE;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void flashrom_serprog_init(const flashrom_serprog_callbacks_t *cb) {
    memset(&s_sp, 0, sizeof(s_sp));
    if (cb != NULL) s_sp.cb = *cb;
    s_sp.state = FLASHROM_SP_IDLE;
}

flashrom_serprog_state_t flashrom_serprog_get_state(void) {
    return s_sp.state;
}

void flashrom_serprog_feed_byte(uint8_t b) {
    switch (s_sp.state) {

    case FLASHROM_SP_IDLE:
        switch (b) {
        case S_CMD_NOP:
            emit(S_ACK);
            return;
        case S_CMD_Q_IFACE:
            emit(S_ACK);
            emit(0x01u);     // serprog v1 — LE 0x0001
            emit(0x00u);
            return;
        case S_CMD_Q_CMDMAP:
            emit(S_ACK);
            emit_n(s_cmdmap, sizeof(s_cmdmap));
            return;
        case S_CMD_Q_PGMNAME:
            emit(S_ACK);
            emit_pgmname();
            return;
        case S_CMD_Q_SERBUF:
            emit(S_ACK);
            emit(0xFFu);
            emit(0xFFu);
            return;
        case S_CMD_Q_BUSTYPE:
            emit(S_ACK);
            emit(FLASHROM_SERPROG_BUSTYPE_SPI);
            return;
        case S_CMD_SYNCNOP:
            // Per spec: NAK + ACK (used to resync host/device).
            emit(S_NAK);
            emit(S_ACK);
            return;
        case S_CMD_S_BUSTYPE:
            s_sp.state = FLASHROM_SP_S_BUSTYPE_1;
            return;
        case S_CMD_O_SPIOP:
            s_sp.spiop_wlen = 0u;
            s_sp.spiop_rlen = 0u;
            s_sp.state = FLASHROM_SP_O_SPIOP_WL_LO;
            return;
        case S_CMD_S_SPI_FREQ:
            s_sp.freq_acc = 0u;
            s_sp.state = FLASHROM_SP_S_SPI_FREQ_1;
            return;
        case S_CMD_S_PIN_STATE:
            s_sp.state = FLASHROM_SP_S_PIN_STATE_1;
            return;
        default:
            emit(S_NAK);
            return;
        }

    case FLASHROM_SP_S_BUSTYPE_1:
        // Host requests a bus mask. We support SPI only — ACK if the
        // SPI bit is set, NAK otherwise. (flashrom's serprog backend
        // always sets exactly the bus it needs.)
        emit((b & FLASHROM_SERPROG_BUSTYPE_SPI) ? S_ACK : S_NAK);
        s_sp.state = FLASHROM_SP_IDLE;
        return;

    case FLASHROM_SP_S_SPI_FREQ_1:
        s_sp.freq_acc = (uint32_t)b;
        s_sp.state = FLASHROM_SP_S_SPI_FREQ_2;
        return;
    case FLASHROM_SP_S_SPI_FREQ_2:
        s_sp.freq_acc |= (uint32_t)b << 8;
        s_sp.state = FLASHROM_SP_S_SPI_FREQ_3;
        return;
    case FLASHROM_SP_S_SPI_FREQ_3:
        s_sp.freq_acc |= (uint32_t)b << 16;
        s_sp.state = FLASHROM_SP_S_SPI_FREQ_4;
        return;
    case FLASHROM_SP_S_SPI_FREQ_4:
        s_sp.freq_acc |= (uint32_t)b << 24;
        if (s_sp.freq_acc == 0u) {
            emit(S_NAK);
        } else {
            // Bit-bang has no programmable clock. Lie politely:
            // report 1 MHz — host treats it as informational and
            // keeps going. Actual rate is whatever the CPU manages
            // (~100 kHz to ~5 MHz depending on inlining and IO
            // speed; well below the 25-series SPI flash limit).
            emit(S_ACK);
            emit_le32(1000000u);
        }
        s_sp.state = FLASHROM_SP_IDLE;
        return;

    case FLASHROM_SP_O_SPIOP_WL_LO:
        s_sp.spiop_wlen = (uint32_t)b;
        s_sp.state = FLASHROM_SP_O_SPIOP_WL_MID;
        return;
    case FLASHROM_SP_O_SPIOP_WL_MID:
        s_sp.spiop_wlen |= (uint32_t)b << 8;
        s_sp.state = FLASHROM_SP_O_SPIOP_WL_HI;
        return;
    case FLASHROM_SP_O_SPIOP_WL_HI:
        s_sp.spiop_wlen |= (uint32_t)b << 16;
        s_sp.state = FLASHROM_SP_O_SPIOP_RL_LO;
        return;
    case FLASHROM_SP_O_SPIOP_RL_LO:
        s_sp.spiop_rlen = (uint32_t)b;
        s_sp.state = FLASHROM_SP_O_SPIOP_RL_MID;
        return;
    case FLASHROM_SP_O_SPIOP_RL_MID:
        s_sp.spiop_rlen |= (uint32_t)b << 8;
        s_sp.state = FLASHROM_SP_O_SPIOP_RL_HI;
        return;
    case FLASHROM_SP_O_SPIOP_RL_HI:
        s_sp.spiop_rlen |= (uint32_t)b << 16;
        // Both lengths now known. Drop CS and either start streaming
        // wbytes (if any) or jump straight to ACK + read.
        spi_cs(true);
        if (s_sp.spiop_wlen == 0u) {
            spiop_finish_with_read_then_cs_high();
        } else {
            s_sp.spiop_done = 0u;
            s_sp.state = FLASHROM_SP_O_SPIOP_WBYTES;
        }
        return;

    case FLASHROM_SP_O_SPIOP_WBYTES:
        // Each incoming byte goes straight to the wire. We discard
        // the MISO byte sampled during the write phase — flashrom
        // doesn't read mid-write.
        (void)spi_xfer(b);
        s_sp.spiop_done++;
        yield_if_due(s_sp.spiop_done - 1u);
        if (s_sp.spiop_done >= s_sp.spiop_wlen) {
            spiop_finish_with_read_then_cs_high();
        }
        return;

    case FLASHROM_SP_S_PIN_STATE_1:
        // Host-facing knob to tristate / enable output drivers. Our
        // bit-bang pins stay configured for the duration of the
        // SERPROG mode session, so this is a no-op + ack regardless
        // of arg.
        emit(S_ACK);
        s_sp.state = FLASHROM_SP_IDLE;
        return;
    }
}

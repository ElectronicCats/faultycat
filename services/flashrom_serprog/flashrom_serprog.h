#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// services/flashrom_serprog — serprog (Serial Flasher Protocol) v1
// over CDC2, streaming flavour. Backed by a 4-pin SPI bit-bang on the
// scanner header (CS / MOSI / MISO / SCK).
//
// What this implements: enough of the spec to drive a typical
// `flashrom -p serprog:dev=/dev/ttyACM2 -r dump.bin` session against
// a 25-series SPI flash chip. Specifically supports:
//
//   S_CMD_NOP        (0x00)  ack
//   S_CMD_Q_IFACE    (0x01)  ack + 0x0001 LE
//   S_CMD_Q_CMDMAP   (0x02)  ack + 32-byte supported-cmds bitmap
//   S_CMD_Q_PGMNAME  (0x03)  ack + "FaultyCat\0..." 16 bytes
//   S_CMD_Q_SERBUF   (0x04)  ack + 0xFFFF (max we'll always handle)
//   S_CMD_Q_BUSTYPE  (0x05)  ack + 0x08 (SPI-only)
//   S_CMD_SYNCNOP    (0x10)  NAK + ACK
//   S_CMD_S_BUSTYPE  (0x12)  ack iff host requested SPI
//   S_CMD_O_SPIOP    (0x13)  3-byte wlen + 3-byte rlen + N wbytes
//                            → CS low, shift wbytes out, ack, shift
//                              rbytes in, CS high
//   S_CMD_S_SPI_FREQ (0x14)  ack + reported actual freq (we lie:
//                            bit-bang has no peripheral freq knob,
//                            we just report 1 MHz)
//   S_CMD_S_PIN_STATE(0x15)  ack (no-op — driver pins always live
//                            while we're in SERPROG mode)
//
// Anything else replies S_NAK so flashrom skips it cleanly.
//
// Algorithm shape adapted from blueTag@v2.1.2 's
// `src/modules/flashProgrammer/serProg.c` (MIT, attribution at file
// head of flashrom_serprog.c). Differences vs upstream:
//
//   1. Streaming, not buffered. Upstream `fread`s wlen bytes into a
//      4 KB stack buffer before handing them to spi_write_blocking;
//      we shift each byte as it arrives so the main loop keeps
//      tud_task / EMFI / crowbar campaigns fed.
//   2. CPU bit-bang, not pico-sdk hardware_spi. Lets the operator
//      pick any 4 of the 8 scanner channels for CS/MOSI/MISO/SCK
//      (HW-SPI mux is fixed to GP0..GP3).
//   3. Callback-based SPI primitive — tests stub it out without
//      simulating a hal_fake_pio FIFO.

typedef struct {
    void    (*write_byte)(uint8_t b, void *user);
    void    (*spi_cs_set)(bool low, void *user);
    uint8_t (*spi_xfer_byte)(uint8_t out, void *user);
    void    (*yield)(void *user);     // optional — called every 128 bytes
                                      // during long SPIOP read/write to keep
                                      // the cooperative main loop alive.
    void    (*on_exit)(void *user);   // F8-5 has no protocol exit byte;
                                      // disconnect detection in main.c
                                      // calls this when CDC2 drops.
    void *user;
} flashrom_serprog_callbacks_t;

void flashrom_serprog_init(const flashrom_serprog_callbacks_t *cb);
void flashrom_serprog_feed_byte(uint8_t b);

typedef enum {
    FLASHROM_SP_IDLE             = 0,
    FLASHROM_SP_S_BUSTYPE_1      = 1,
    FLASHROM_SP_S_SPI_FREQ_1     = 2,
    FLASHROM_SP_S_SPI_FREQ_2     = 3,
    FLASHROM_SP_S_SPI_FREQ_3     = 4,
    FLASHROM_SP_S_SPI_FREQ_4     = 5,
    FLASHROM_SP_O_SPIOP_WL_LO    = 6,
    FLASHROM_SP_O_SPIOP_WL_MID   = 7,
    FLASHROM_SP_O_SPIOP_WL_HI    = 8,
    FLASHROM_SP_O_SPIOP_RL_LO    = 9,
    FLASHROM_SP_O_SPIOP_RL_MID   = 10,
    FLASHROM_SP_O_SPIOP_RL_HI    = 11,
    FLASHROM_SP_O_SPIOP_WBYTES   = 12,
    FLASHROM_SP_S_PIN_STATE_1    = 13,
} flashrom_serprog_state_t;

flashrom_serprog_state_t flashrom_serprog_get_state(void);

// Programmer name reported on S_CMD_Q_PGMNAME (16 bytes total,
// NUL-padded). Exposed for tests + docs.
#define FLASHROM_SERPROG_PGMNAME "FaultyCat"

// S_CMD_Q_BUSTYPE bus mask. SPI = 0x08.
#define FLASHROM_SERPROG_BUSTYPE_SPI 0x08u

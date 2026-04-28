/*
 * services/buspirate_compat/buspirate_compat.c — BusPirate v1 BBIO +
 * OpenOCD JTAG sub-mode, streaming flavour.
 *
 * Algorithm shape adapted from third_party/blueTag @ v2.1.2's
 * `src/modules/openocd/openocdJTAG.c` (MIT). Key differences from
 * upstream:
 *
 *   1. Streaming, not buffered. Upstream blocks in getc(stdin) and
 *      stages 4 KB of (TDI, TMS) byte pairs per TAP_SHIFT before
 *      processing; we consume one byte at a time so the main loop
 *      keeps tud_task / EMFI / crowbar campaigns fed.
 *   2. Callback-based JTAG clocker — services/jtag_core's
 *      jtag_clock_bit is wired in by the shell at runtime; tests
 *      stub it out.
 *   3. CMD_UNKNOWN (0x00) inside OOCD takes us back to BBIO_IDLE
 *      (matching upstream's `return` after writing "BBIO1").
 *
 * --- MIT License (blueTag excerpt) -----------------------------------
 *   The MIT License (MIT)
 *   Copyright (c) 2024 Aodrulez
 *   (full text at third_party/blueTag/LICENSE +
 *    LICENSES/UPSTREAM-blueTag.txt)
 * --------------------------------------------------------------------
 */

#include "buspirate_compat.h"

#include <stdint.h>

// BBIO entry-mode bytes (BusPirate v1.x binary protocol §2).
#define BP_CMD_BBIO_RESET       0x00u   // → "BBIO1"
#define BP_CMD_ENTER_OOCD       0x06u   // → "OCD1"  (ENTER_OPENOCD)
#define BP_CMD_USER_TERM        0x0Fu   // exit BBIO back to text shell

// OpenOCD sub-commands (numbered relative to blueTag's openocdJTAG.c
// constants — those drive the OpenOCD `interface/buspirate.cfg`
// driver wire format).
#define BP_OCD_UNKNOWN          0x00u   // "go back to BBIO"
#define BP_OCD_PORT_MODE        0x01u   // 1 arg byte
#define BP_OCD_FEATURE          0x02u   // 2 arg bytes
#define BP_OCD_READ_ADCS        0x03u   // 0 arg, 10-byte reply
#define BP_OCD_TAP_SHIFT        0x05u   // 2-byte len + 2*ceil(n/8) data
#define BP_OCD_ENTER_OOCD       0x06u   // re-enter; replies "OCD1"
#define BP_OCD_UART_SPEED       0x07u   // 3 arg bytes, 2-byte reply
#define BP_OCD_JTAG_SPEED       0x08u   // 2 arg bytes, no reply

// -----------------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------------

typedef struct {
    buspirate_compat_callbacks_t cb;
    buspirate_compat_state_t     state;

    // CMD_TAP_SHIFT streaming state. Reset on each new TAP_SHIFT.
    uint16_t tap_bits_total;
    uint16_t tap_bits_done;
    uint8_t  tap_tdi_byte;
    uint8_t  tap_tdo_byte;
    uint8_t  tap_bit_in_byte;
} bp_t;

static bp_t s_bp;

// -----------------------------------------------------------------------------
// Output helpers
// -----------------------------------------------------------------------------

static void emit(uint8_t b) {
    if (s_bp.cb.write_byte) s_bp.cb.write_byte(b, s_bp.cb.user);
}

static void emit_str(const char *s) {
    while (*s) emit((uint8_t)*s++);
}

static bool clock_bit(bool tms, bool tdi) {
    if (s_bp.cb.jtag_clock_bit) {
        return s_bp.cb.jtag_clock_bit(tms, tdi, s_bp.cb.user);
    }
    return false;
}

// -----------------------------------------------------------------------------
// CMD_TAP_SHIFT helpers — process one (TDI, TMS) byte pair into TDO.
// -----------------------------------------------------------------------------

// Begin a fresh TAP_SHIFT — caller has just received both length bytes.
static void tap_shift_begin(uint16_t total) {
    if (total > BUSPIRATE_TAP_SHIFT_MAX_BITS) {
        total = BUSPIRATE_TAP_SHIFT_MAX_BITS;
    }
    s_bp.tap_bits_total  = total;
    s_bp.tap_bits_done   = 0;
    s_bp.tap_tdi_byte    = 0;
    s_bp.tap_tdo_byte    = 0;
    s_bp.tap_bit_in_byte = 0;

    // Echo the header per blueTag/OpenOCD convention so the host
    // confirms the length we agreed on (post-clamp).
    emit(BP_OCD_TAP_SHIFT);
    emit((uint8_t)(total >> 8));
    emit((uint8_t)(total & 0xFFu));
}

// Process up to 8 bits from a single (TDI, TMS) byte pair. Emits one
// output TDO byte once the pair is consumed (or the total is hit
// mid-byte). Returns true if the TAP_SHIFT is done.
static bool tap_shift_pair(uint8_t tdi_byte, uint8_t tms_byte) {
    uint16_t remaining = (uint16_t)(s_bp.tap_bits_total - s_bp.tap_bits_done);
    uint8_t  bits_this = (remaining >= 8u) ? 8u : (uint8_t)remaining;

    for (uint8_t b = 0; b < bits_this; b++) {
        bool tdi = (bool)((tdi_byte >> b) & 1u);
        bool tms = (bool)((tms_byte >> b) & 1u);
        bool tdo = clock_bit(tms, tdi);

        // Output bit packing mirrors input — LSB-first within the
        // byte, indexed by `tap_bit_in_byte` (which equals `b` for
        // a full 8-bit pair, but stays sane across partial bytes).
        if (tdo) {
            s_bp.tap_tdo_byte |= (uint8_t)(1u << s_bp.tap_bit_in_byte);
        }
        s_bp.tap_bit_in_byte++;
        s_bp.tap_bits_done++;

        bool last_bit = (s_bp.tap_bits_done == s_bp.tap_bits_total);
        if (s_bp.tap_bit_in_byte == 8u || last_bit) {
            emit(s_bp.tap_tdo_byte);
            s_bp.tap_tdo_byte    = 0;
            s_bp.tap_bit_in_byte = 0;
        }
    }
    return (s_bp.tap_bits_done >= s_bp.tap_bits_total);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void buspirate_compat_init(const buspirate_compat_callbacks_t *cb) {
    s_bp.state = BUSPIRATE_BBIO_IDLE;
    if (cb != NULL) s_bp.cb = *cb;
    s_bp.tap_bits_total  = 0;
    s_bp.tap_bits_done   = 0;
    s_bp.tap_tdi_byte    = 0;
    s_bp.tap_tdo_byte    = 0;
    s_bp.tap_bit_in_byte = 0;
}

buspirate_compat_state_t buspirate_compat_get_state(void) {
    return s_bp.state;
}

void buspirate_compat_feed_byte(uint8_t b) {
    switch (s_bp.state) {

    case BUSPIRATE_BBIO_IDLE:
        // BBIO mode entry table. We support the OpenOCD entry path
        // (0x06 → OCD1) and the user-terminal escape (0x0F). Every
        // other 0x00..0x0E byte falls through to "BBIO1" so the
        // OpenOCD probe loop (sends 0x00 ×25) sees a stable reply.
        if (b == BP_CMD_USER_TERM) {
            if (s_bp.cb.on_exit) s_bp.cb.on_exit(s_bp.cb.user);
            s_bp.state = BUSPIRATE_BBIO_IDLE;   // ready for next session
            return;
        }
        if (b == BP_CMD_ENTER_OOCD) {
            emit_str("OCD1");
            s_bp.state = BUSPIRATE_OCD_IDLE;
            return;
        }
        // Default — including BP_CMD_BBIO_RESET — replies BBIO1.
        emit_str("BBIO1");
        return;

    case BUSPIRATE_OCD_IDLE:
        switch (b) {
        case BP_OCD_UNKNOWN:
            // Per blueTag, this returns to BBIO and re-introduces.
            emit_str("BBIO1");
            s_bp.state = BUSPIRATE_BBIO_IDLE;
            return;
        case BP_OCD_ENTER_OOCD:
            emit_str("OCD1");
            return;
        case BP_OCD_PORT_MODE:
            s_bp.state = BUSPIRATE_OCD_PORT_MODE_1;
            return;
        case BP_OCD_FEATURE:
            s_bp.state = BUSPIRATE_OCD_FEATURE_1;
            return;
        case BP_OCD_READ_ADCS: {
            // Reply 10 bytes: [cmd, 8 (count), 0×8] — we don't expose
            // any ADC in OOCD mode (target_monitor is for glitch
            // campaigns, not driver telemetry), but OpenOCD tolerates
            // zeros.
            emit(BP_OCD_READ_ADCS);
            emit(8u);
            for (uint8_t i = 0; i < 8u; i++) emit(0u);
            return;
        }
        case BP_OCD_TAP_SHIFT:
            s_bp.state = BUSPIRATE_OCD_TAP_LEN_HI;
            return;
        case BP_OCD_UART_SPEED:
            s_bp.state = BUSPIRATE_OCD_UART_SPEED_1;
            return;
        case BP_OCD_JTAG_SPEED:
            s_bp.state = BUSPIRATE_OCD_JTAG_SPEED_1;
            return;
        case BP_CMD_USER_TERM:
            if (s_bp.cb.on_exit) s_bp.cb.on_exit(s_bp.cb.user);
            s_bp.state = BUSPIRATE_BBIO_IDLE;
            return;
        default:
            // Unknown OOCD subcmd → emit a single zero byte. Matches
            // blueTag's default arm for OpenOCD compatibility.
            emit(0u);
            return;
        }

    case BUSPIRATE_OCD_PORT_MODE_1:
        // Argument byte consumed, no reply (per blueTag).
        s_bp.state = BUSPIRATE_OCD_IDLE;
        return;

    case BUSPIRATE_OCD_FEATURE_1:
        s_bp.state = BUSPIRATE_OCD_FEATURE_2;
        return;
    case BUSPIRATE_OCD_FEATURE_2:
        s_bp.state = BUSPIRATE_OCD_IDLE;
        return;

    case BUSPIRATE_OCD_TAP_LEN_HI:
        s_bp.tap_bits_total = (uint16_t)((uint16_t)b << 8);
        s_bp.state = BUSPIRATE_OCD_TAP_LEN_LO;
        return;
    case BUSPIRATE_OCD_TAP_LEN_LO: {
        uint16_t total = (uint16_t)(s_bp.tap_bits_total | b);
        tap_shift_begin(total);
        if (s_bp.tap_bits_total == 0u) {
            // Edge case: empty TAP_SHIFT. Echo header only.
            s_bp.state = BUSPIRATE_OCD_IDLE;
        } else {
            s_bp.state = BUSPIRATE_OCD_TAP_TDI;
        }
        return;
    }
    case BUSPIRATE_OCD_TAP_TDI:
        s_bp.tap_tdi_byte = b;
        s_bp.state = BUSPIRATE_OCD_TAP_TMS;
        return;
    case BUSPIRATE_OCD_TAP_TMS: {
        bool done = tap_shift_pair(s_bp.tap_tdi_byte, b);
        s_bp.state = done ? BUSPIRATE_OCD_IDLE : BUSPIRATE_OCD_TAP_TDI;
        return;
    }

    case BUSPIRATE_OCD_UART_SPEED_1:
        s_bp.state = BUSPIRATE_OCD_UART_SPEED_2;
        return;
    case BUSPIRATE_OCD_UART_SPEED_2:
        s_bp.state = BUSPIRATE_OCD_UART_SPEED_3;
        return;
    case BUSPIRATE_OCD_UART_SPEED_3:
        // 3 arg bytes consumed; emit [cmd, 0=normal-serial] and
        // return to OCD idle.
        emit(BP_OCD_UART_SPEED);
        emit(0u);
        s_bp.state = BUSPIRATE_OCD_IDLE;
        return;

    case BUSPIRATE_OCD_JTAG_SPEED_1:
        s_bp.state = BUSPIRATE_OCD_JTAG_SPEED_2;
        return;
    case BUSPIRATE_OCD_JTAG_SPEED_2:
        s_bp.state = BUSPIRATE_OCD_IDLE;
        return;
    }
}

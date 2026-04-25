#pragma once

// FaultyCat HW v2.x pin map — authoritative single source of truth.
//
// Every driver and every app consumes this header. It replaces the
// legacy `firmware/c/board_config.h`, which was Pico-module-oriented
// and carried stale faultier relics (PIN_EXT1 = 27 collided with the
// charge LED). See docs/HARDWARE_V2.md for full context and
// verification notes.
//
// Production target: v2.2 (v2.1 → v2.2 = label changes only, no nets).
// All `BOARD_GP_*` values are RP2040 GPIO numbers.

#include <stdint.h>

// -----------------------------------------------------------------------------
// LEDs — 3 physical indicators on v2.2 (confirmed 2026-04-23)
// -----------------------------------------------------------------------------
#define BOARD_GP_LED_HV_DETECTED    9u   // lit while HV cap is charged
#define BOARD_GP_LED_STATUS         10u  // general-purpose status
#define BOARD_GP_LED_CHARGE_ON      27u  // lit while flyback is pushing charge

// -----------------------------------------------------------------------------
// Buttons
// -----------------------------------------------------------------------------
#define BOARD_GP_BTN_ARM            28u  // active-high, internal pulldown
#define BOARD_GP_BTN_PULSE          11u  // active-low, internal pullup + input-invert

// -----------------------------------------------------------------------------
// Scanner header (Conn_01x10: 8 signals + VCC + GND)
// -----------------------------------------------------------------------------
#define BOARD_GP_SCANNER_CH0        0u
#define BOARD_GP_SCANNER_CH1        1u
#define BOARD_GP_SCANNER_CH2        2u
#define BOARD_GP_SCANNER_CH3        3u
#define BOARD_GP_SCANNER_CH4        4u
#define BOARD_GP_SCANNER_CH5        5u
#define BOARD_GP_SCANNER_CH6        6u
#define BOARD_GP_SCANNER_CH7        7u
#define BOARD_SCANNER_CHANNEL_COUNT 8u

// -----------------------------------------------------------------------------
// External trigger (v2.1+)
// -----------------------------------------------------------------------------
#define BOARD_GP_TRIGGER_IN         8u   // level-shifted via TRIGGER_VREF
// Alias used by services/glitch_engine/emfi/emfi_pio for PIO in-pin
// binding. Same physical net as BOARD_GP_TRIGGER_IN.
#define BOARD_GP_EXT_TRIGGER        BOARD_GP_TRIGGER_IN

// -----------------------------------------------------------------------------
// SWD over scanner header — F6 swd_core defaults. SWD/JTAG/scanner
// share GP0..GP7; only one of {scanner_io, swd_core, jtag_core (F8),
// pinout_scanner (F8)} may own a given pin at a time. F9 lands the
// formal mutex; F6 documents the contract.
// -----------------------------------------------------------------------------
#define BOARD_GP_SWCLK_DEFAULT      BOARD_GP_SCANNER_CH0
#define BOARD_GP_SWDIO_DEFAULT      BOARD_GP_SCANNER_CH1
#define BOARD_GP_SWRST_DEFAULT      BOARD_GP_SCANNER_CH2

// -----------------------------------------------------------------------------
// Crowbar — voltage glitching paths (v2.1+). Software selects between
// the low-power path and the real N-MOSFET path; there is NO hardware
// multiplexer on v2.x (confirmed by maintainer 2026-04-23).
// -----------------------------------------------------------------------------
#define BOARD_GP_CROWBAR_LP         16u  // low-power shorting path
#define BOARD_GP_CROWBAR_HP         17u  // IRLML0060 N-MOSFET gate (REAL glitch)

// -----------------------------------------------------------------------------
// EMFI pulse (PIO-driven HV pulse out) — HV DOMAIN
// -----------------------------------------------------------------------------
#define BOARD_GP_HV_PULSE           14u

// -----------------------------------------------------------------------------
// HV charger (flyback) — HV DOMAIN
// -----------------------------------------------------------------------------
#define BOARD_GP_HV_PWM             20u  // ~2.5 kHz flyback PWM drive
#define BOARD_GP_HV_CHARGED         18u  // "charged" feedback — ACTIVE LOW

// -----------------------------------------------------------------------------
// Target monitor (v2.1+)
// -----------------------------------------------------------------------------
#define BOARD_GP_TARGET_ADC         29u  // mapped to ADC channel
#define BOARD_TARGET_ADC_CHANNEL    3u

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "emfi_pio.h"   // for emfi_trig_t

// services/glitch_engine/emfi/emfi_campaign — orchestrates the
// EMFI fire path end-to-end. Owns the state machine, enforces the
// SAFETY.md §3 item 5 invariant (HV charged within 100 ms before
// PIO start), and teardown on every terminal state.
//
// Drivers/services used (composed in strict layer order):
//   drivers/hv_charger     — arm, poll CHARGED, disarm
//   drivers/ext_trigger    — already initialized by main; read-only
//   services/emfi_pio      — PIO fire path
//   services/emfi_capture  — ADC DMA ring
//   drivers/emfi_pulse     — attach/detach of GP14 (via emfi_pio)
//
// Public API is poll-based: fire() is non-blocking, tick() advances
// the state machine from the main loop. Host callers get status via
// get_status(); capture buffer via capture_buffer()/capture_len().

typedef struct {
    emfi_trig_t trigger;
    uint32_t    delay_us;
    uint32_t    width_us;
    uint32_t    charge_timeout_ms;   // 0 = wait up to hv_charger auto-disarm (60 s)
} emfi_config_t;

typedef enum {
    EMFI_STATE_IDLE    = 0,
    EMFI_STATE_ARMING  = 1,
    EMFI_STATE_CHARGED = 2,
    EMFI_STATE_WAITING = 3,   // trigger wait + PIO running
    EMFI_STATE_FIRED   = 4,
    EMFI_STATE_ERROR   = 5,
} emfi_state_t;

typedef enum {
    EMFI_ERR_NONE             = 0,
    EMFI_ERR_BAD_CONFIG       = 1,
    EMFI_ERR_HV_NOT_CHARGED   = 2,
    EMFI_ERR_TRIGGER_TIMEOUT  = 3,
    EMFI_ERR_PIO_FAULT        = 4,
    EMFI_ERR_INTERNAL         = 5,
} emfi_err_t;

typedef struct {
    emfi_state_t state;
    emfi_err_t   err;
    uint32_t     last_fire_at_ms;
    uint32_t     capture_fill;
    uint32_t     pulse_width_us_actual;
    uint32_t     delay_us_actual;
} emfi_status_t;

// SAFETY constant — the HV-within-100 ms window per SAFETY.md §3 #5.
// Exposed for tests and for diagnostics.
#define EMFI_CAMPAIGN_HV_STALE_MS 100u

bool emfi_campaign_init(void);
bool emfi_campaign_configure(const emfi_config_t *cfg);
bool emfi_campaign_arm(void);
bool emfi_campaign_fire(uint32_t trigger_timeout_ms);
void emfi_campaign_disarm(void);
void emfi_campaign_tick(void);
void emfi_campaign_get_status(emfi_status_t *out);
const uint8_t *emfi_campaign_capture_buffer(void);
uint32_t       emfi_campaign_capture_len(void);

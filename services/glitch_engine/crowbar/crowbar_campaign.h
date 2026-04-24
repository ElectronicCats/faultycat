#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "crowbar_pio.h"   // for crowbar_trig_t, crowbar_out_t

// services/glitch_engine/crowbar/crowbar_campaign — orchestrates the
// crowbar fire path end-to-end. Owns the state machine and enforces
// gate ownership: outside the fire window the path is owned by
// drivers/crowbar_mosfet (idle = NONE, break-before-make on
// transitions); during the fire window crowbar_pio drives the
// selected gate.
//
// Crowbar does NOT use the HV cap — the SAFETY.md §3 #5 invariant
// (HV charged within 100 ms) is EMFI-only. This service has no HV
// state at all.
//
// Drivers/services used (composed in strict layer order):
//   drivers/crowbar_mosfet  — gate idle state + break-before-make
//   drivers/ext_trigger     — already initialized by main; read-only
//   services/crowbar_pio    — PIO fire path
//
// Public API is poll-based: fire() is non-blocking, tick() advances
// the state machine from the main loop.

typedef struct {
    crowbar_trig_t trigger;
    crowbar_out_t  output;       // CROWBAR_OUT_LP or CROWBAR_OUT_HP
    uint32_t       delay_us;     // 0..CROWBAR_PIO_DELAY_US_MAX
    uint32_t       width_ns;     // CROWBAR_PIO_WIDTH_NS_MIN..MAX
} crowbar_config_t;

typedef enum {
    CROWBAR_STATE_IDLE    = 0,
    CROWBAR_STATE_ARMING  = 1,   // path validation + driver set_path(NONE)
    CROWBAR_STATE_ARMED   = 2,   // ready to fire
    CROWBAR_STATE_WAITING = 3,   // PIO running; trigger wait + pulse
    CROWBAR_STATE_FIRED   = 4,
    CROWBAR_STATE_ERROR   = 5,
} crowbar_state_t;

typedef enum {
    CROWBAR_ERR_NONE              = 0,
    CROWBAR_ERR_BAD_CONFIG        = 1,
    CROWBAR_ERR_TRIGGER_TIMEOUT   = 2,
    CROWBAR_ERR_PIO_FAULT         = 3,
    CROWBAR_ERR_INTERNAL          = 4,
    CROWBAR_ERR_PATH_NOT_SELECTED = 5,   // fire requested with OUT_NONE
} crowbar_err_t;

typedef struct {
    crowbar_state_t state;
    crowbar_err_t   err;
    uint32_t        last_fire_at_ms;
    uint32_t        pulse_width_ns_actual;
    uint32_t        delay_us_actual;
    crowbar_out_t   output;             // gate used by the most recent fire
} crowbar_status_t;

bool crowbar_campaign_init(void);
bool crowbar_campaign_configure(const crowbar_config_t *cfg);
bool crowbar_campaign_arm(void);
bool crowbar_campaign_fire(uint32_t trigger_timeout_ms);
void crowbar_campaign_disarm(void);
void crowbar_campaign_tick(void);
void crowbar_campaign_get_status(crowbar_status_t *out);

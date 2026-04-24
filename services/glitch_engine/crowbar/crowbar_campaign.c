#include "crowbar_campaign.h"

#include <string.h>

#include "crowbar_mosfet.h"
#include "crowbar_pio.h"
#include "hal/time.h"

static crowbar_config_t s_cfg;
static bool             s_cfg_valid = false;
static crowbar_status_t s_status;
static uint32_t         s_fire_start_ms;
static uint32_t         s_fire_timeout_ms;

static void reset_status(void) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state  = CROWBAR_STATE_IDLE;
    s_status.output = CROWBAR_OUT_NONE;
}

static void teardown(void) {
    crowbar_pio_clear_done();
    crowbar_pio_deinit();
    // Re-assert idle gate state. PIO release leaves the pin as a
    // plain GPIO; force the driver-side shadow back to NONE so the
    // break-before-make invariant (SAFETY.md §3 #3) holds while the
    // service is between fires.
    crowbar_mosfet_set_path(CROWBAR_PATH_NONE);
}

static void enter_error(crowbar_err_t err) {
    teardown();
    s_status.state = CROWBAR_STATE_ERROR;
    s_status.err   = err;
}

static bool valid_trigger(crowbar_trig_t t) {
    switch (t) {
        case CROWBAR_TRIG_IMMEDIATE:
        case CROWBAR_TRIG_EXT_RISING:
        case CROWBAR_TRIG_EXT_FALLING:
        case CROWBAR_TRIG_EXT_PULSE_POS:
            return true;
    }
    return false;
}

static bool validate_cfg(const crowbar_config_t *c) {
    if (!c) return false;
    if (!valid_trigger(c->trigger)) return false;
    if (c->output != CROWBAR_OUT_LP && c->output != CROWBAR_OUT_HP) return false;
    if (c->width_ns < CROWBAR_PIO_WIDTH_NS_MIN
     || c->width_ns > CROWBAR_PIO_WIDTH_NS_MAX) return false;
    if (c->delay_us > CROWBAR_PIO_DELAY_US_MAX) return false;
    return true;
}

static crowbar_path_t out_to_path(crowbar_out_t o) {
    switch (o) {
        case CROWBAR_OUT_LP: return CROWBAR_PATH_LP;
        case CROWBAR_OUT_HP: return CROWBAR_PATH_HP;
        default:             return CROWBAR_PATH_NONE;
    }
}

bool crowbar_campaign_init(void) {
    reset_status();
    s_cfg_valid = false;
    return true;
}

bool crowbar_campaign_configure(const crowbar_config_t *cfg) {
    if (!validate_cfg(cfg)) return false;
    s_cfg          = *cfg;
    s_cfg_valid    = true;
    // A fresh configure clears any stale ERROR so the operator does
    // not need to disarm explicitly to retry with new params.
    s_status.state = CROWBAR_STATE_IDLE;
    s_status.err   = CROWBAR_ERR_NONE;
    return true;
}

bool crowbar_campaign_arm(void) {
    if (!s_cfg_valid) {
        enter_error(CROWBAR_ERR_BAD_CONFIG);
        return false;
    }
    if (s_status.state != CROWBAR_STATE_IDLE
     && s_status.state != CROWBAR_STATE_FIRED) return false;

    // First half of the gate hand-off invariant: declare ARMED only
    // after both gates are LOW. If something else (manual diag, demo
    // path, leftover from a previous fire that didn't tear down
    // cleanly) had asserted LP or HP, this drops it before the
    // operator thinks the service is ready.
    crowbar_mosfet_set_path(CROWBAR_PATH_NONE);
    s_status.state = CROWBAR_STATE_ARMED;
    s_status.err   = CROWBAR_ERR_NONE;
    return true;
}

bool crowbar_campaign_fire(uint32_t trigger_timeout_ms) {
    if (s_status.state != CROWBAR_STATE_ARMED) return false;

    // Defense-in-depth: re-assert NONE right before the PIO grabs
    // the gate, in case anything changed driver state between arm()
    // and fire(). Cheap (two GPIO writes) and the only thing
    // protecting the operator if the layering is bypassed.
    crowbar_mosfet_set_path(CROWBAR_PATH_NONE);

    // configure() rejects OUT_NONE, so this is a pure invariant
    // check. Kept so a future bypass of configure() (direct write to
    // s_cfg from a test, or a new code path) cannot fire blind.
    if (s_cfg.output != CROWBAR_OUT_LP && s_cfg.output != CROWBAR_OUT_HP) {
        enter_error(CROWBAR_ERR_PATH_NOT_SELECTED);
        return false;
    }

    if (!crowbar_pio_init()) {
        enter_error(CROWBAR_ERR_PIO_FAULT);
        return false;
    }
    crowbar_pio_params_t pp = {
        .trigger  = s_cfg.trigger,
        .output   = s_cfg.output,
        .delay_us = s_cfg.delay_us,
        .width_ns = s_cfg.width_ns,
    };
    if (!crowbar_pio_load(&pp)) {
        enter_error(CROWBAR_ERR_PIO_FAULT);
        return false;
    }
    if (!crowbar_pio_start()) {
        enter_error(CROWBAR_ERR_PIO_FAULT);
        return false;
    }

    s_fire_start_ms   = hal_now_ms();
    s_fire_timeout_ms = trigger_timeout_ms;
    s_status.state    = CROWBAR_STATE_WAITING;
    s_status.err      = CROWBAR_ERR_NONE;
    return true;
}

void crowbar_campaign_disarm(void) {
    teardown();
    reset_status();
    // s_cfg_valid intentionally preserved — disarm tears down the
    // fire path but should not erase the operator's last good config.
}

static void tick_waiting(void) {
    if (crowbar_pio_is_done()) {
        s_status.pulse_width_ns_actual = s_cfg.width_ns;
        s_status.delay_us_actual       = s_cfg.delay_us;
        s_status.output                = s_cfg.output;
        s_status.last_fire_at_ms       = hal_now_ms();
        teardown();
        s_status.state = CROWBAR_STATE_FIRED;
        return;
    }
    if (s_fire_timeout_ms == 0u) return;   // 0 = wait forever
    uint32_t elapsed = hal_now_ms() - s_fire_start_ms;
    if (elapsed > s_fire_timeout_ms) {
        enter_error(CROWBAR_ERR_TRIGGER_TIMEOUT);
    }
}

void crowbar_campaign_tick(void) {
    switch (s_status.state) {
        case CROWBAR_STATE_WAITING: tick_waiting(); break;
        default: break;
    }
}

void crowbar_campaign_get_status(crowbar_status_t *out) {
    if (!out) return;
    *out = s_status;
}

#include "emfi_campaign.h"

#include <string.h>

#include "emfi_capture.h"
#include "emfi_pio.h"
#include "emfi_pulse.h"
#include "ext_trigger.h"
#include "hal/time.h"
#include "hv_charger.h"

static emfi_config_t s_cfg;
static bool          s_cfg_valid = false;
static emfi_status_t s_status;
static uint32_t      s_arm_start_ms;
static uint32_t      s_fire_start_ms;
static uint32_t      s_fire_timeout_ms;
static uint32_t      s_hv_last_charged_ms;

static void reset_status(void) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = EMFI_STATE_IDLE;
}

static void teardown(void) {
    // Always safe to call, regardless of which pieces were actually started.
    emfi_capture_stop();
    emfi_pio_clear_done();
    emfi_pio_deinit();
    emfi_pulse_detach_pio();
    hv_charger_disarm();
}

static void enter_error(emfi_err_t err) {
    teardown();
    s_status.state = EMFI_STATE_ERROR;
    s_status.err   = err;
}

bool emfi_campaign_init(void) {
    reset_status();
    s_cfg_valid        = false;
    s_hv_last_charged_ms = 0;
    // emfi_capture claims its DMA channel lazily; call it here so the
    // first fire() doesn't silently skip capture_start on a fresh boot.
    if (!emfi_capture_init()) {
        return false;
    }
    return true;
}

bool emfi_campaign_configure(const emfi_config_t *cfg) {
    if (!cfg) return false;
    if (cfg->width_us < 1u || cfg->width_us > 50u) return false;
    if (cfg->delay_us > 1000000u) return false;
    s_cfg       = *cfg;
    s_cfg_valid = true;
    // Clearing any error from a prior run lets a fresh configure
    // drop the service back into IDLE.
    s_status.state = EMFI_STATE_IDLE;
    s_status.err   = EMFI_ERR_NONE;
    return true;
}

bool emfi_campaign_arm(void) {
    if (!s_cfg_valid) {
        enter_error(EMFI_ERR_BAD_CONFIG);
        return false;
    }
    if (s_status.state != EMFI_STATE_IDLE) return false;

    hv_charger_arm();
    s_arm_start_ms = hal_now_ms();
    s_status.state = EMFI_STATE_ARMING;
    s_status.err   = EMFI_ERR_NONE;
    return true;
}

bool emfi_campaign_fire(uint32_t trigger_timeout_ms) {
    if (s_status.state != EMFI_STATE_CHARGED) return false;

    // Re-check invariant right here: if HV was last seen charged too
    // long ago, fail fast rather than firing into a half-spent cap.
    uint32_t now = hal_now_ms();
    if ((now - s_hv_last_charged_ms) > EMFI_CAMPAIGN_HV_STALE_MS) {
        // Resample once in case the bit has returned.
        if (hv_charger_is_charged()) {
            s_hv_last_charged_ms = now;
        } else {
            enter_error(EMFI_ERR_HV_NOT_CHARGED);
            return false;
        }
    }

    if (!emfi_pio_init()) {
        enter_error(EMFI_ERR_PIO_FAULT);
        return false;
    }
    emfi_pio_params_t pp = {
        .trigger  = s_cfg.trigger,
        .delay_us = s_cfg.delay_us,
        .width_us = s_cfg.width_us,
    };
    if (!emfi_pio_load(&pp)) {
        enter_error(EMFI_ERR_PIO_FAULT);
        return false;
    }
    emfi_capture_start();
    if (!emfi_pio_start()) {
        enter_error(EMFI_ERR_PIO_FAULT);
        return false;
    }

    s_fire_start_ms   = now;
    s_fire_timeout_ms = trigger_timeout_ms;
    s_status.state    = EMFI_STATE_WAITING;
    return true;
}

void emfi_campaign_disarm(void) {
    teardown();
    reset_status();
}

static void tick_arming(void) {
    if (hv_charger_is_charged()) {
        s_status.state       = EMFI_STATE_CHARGED;
        s_hv_last_charged_ms = hal_now_ms();
        return;
    }
    uint32_t elapsed = hal_now_ms() - s_arm_start_ms;
    uint32_t timeout = s_cfg.charge_timeout_ms;
    if (timeout == 0u) return;  // 0 = wait up to hv_charger auto-disarm
    if (elapsed > timeout) {
        enter_error(EMFI_ERR_HV_NOT_CHARGED);
    }
}

static void tick_charged(void) {
    // Keep the "last charged" timestamp fresh while sitting in CHARGED
    // state, so the 100 ms invariant at fire() time reflects reality.
    if (hv_charger_is_charged()) {
        s_hv_last_charged_ms = hal_now_ms();
    }
}

static void tick_waiting(void) {
    if (emfi_pio_is_done()) {
        s_status.capture_fill         = emfi_capture_fill();
        s_status.pulse_width_us_actual = s_cfg.width_us;
        s_status.delay_us_actual      = s_cfg.delay_us;
        s_status.last_fire_at_ms      = hal_now_ms();
        teardown();
        s_status.state = EMFI_STATE_FIRED;
        return;
    }
    if (s_fire_timeout_ms == 0u) return;
    uint32_t elapsed = hal_now_ms() - s_fire_start_ms;
    if (elapsed > s_fire_timeout_ms) {
        enter_error(EMFI_ERR_TRIGGER_TIMEOUT);
    }
}

void emfi_campaign_tick(void) {
    hv_charger_tick();  // let the HV driver run its 60 s safety net.
    switch (s_status.state) {
        case EMFI_STATE_ARMING:  tick_arming();  break;
        case EMFI_STATE_CHARGED: tick_charged(); break;
        case EMFI_STATE_WAITING: tick_waiting(); break;
        default: break;
    }
}

void emfi_campaign_get_status(emfi_status_t *out) {
    if (!out) return;
    *out = s_status;
}

const uint8_t *emfi_campaign_capture_buffer(void) {
    return emfi_capture_buffer();
}

uint32_t emfi_campaign_capture_len(void) {
    return s_status.capture_fill;
}

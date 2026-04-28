/*
 * services/campaign_manager/campaign_manager.c — F9-2 sweep
 * orchestrator. State machine + cartesian sweep generator +
 * 24-byte/entry result ringbuffer + pluggable step executor.
 *
 * F9-3 will wire `campaign_manager_set_step_executor` to a real
 * adapter that drives `services/glitch_engine/{emfi,crowbar}`.
 */

#include "campaign_manager.h"

#include <string.h>

#include "hal/time.h"

// -----------------------------------------------------------------------------
// Module state
// -----------------------------------------------------------------------------

typedef struct {
    campaign_state_t          state;
    campaign_err_t            err;
    campaign_config_t         cfg;
    bool                      cfg_valid;
    uint32_t                  step_n;
    uint32_t                  total_steps;
    uint32_t                  results_pushed;
    uint32_t                  results_dropped;
    uint32_t                  last_step_at_ms;

    campaign_step_executor_t  executor;
    void                     *executor_user;

    // Ringbuffer — single producer (campaign_manager_tick), single
    // drainer (campaign_manager_drain_results from host_proto).
    // Cooperative single-core, so plain head/tail without atomics.
    campaign_result_t         ring[CAMPAIGN_RESULT_RING_DEPTH];
    uint32_t                  ring_head;       // write index
    uint32_t                  ring_tail;       // read index
    uint32_t                  ring_count;      // 0..CAMPAIGN_RESULT_RING_DEPTH
} cm_t;

static cm_t s_cm;

// -----------------------------------------------------------------------------
// Pure helpers
// -----------------------------------------------------------------------------

uint32_t campaign_axis_step_count(const campaign_axis_t *axis) {
    if (axis == NULL) return 0u;
    if (axis->step == 0u) return 1u;            // collapse: only `start`
    if (axis->start > axis->end) return 0u;
    return ((axis->end - axis->start) / axis->step) + 1u;
}

uint32_t campaign_total_steps(const campaign_config_t *cfg) {
    if (cfg == NULL) return 0u;
    uint32_t d = campaign_axis_step_count(&cfg->delay);
    uint32_t w = campaign_axis_step_count(&cfg->width);
    uint32_t p = campaign_axis_step_count(&cfg->power);
    if (d == 0u || w == 0u || p == 0u) return 0u;
    return d * w * p;
}

static uint32_t axis_value_at(const campaign_axis_t *axis, uint32_t idx) {
    // idx must already be < axis_step_count.
    return axis->start + (idx * axis->step);
}

bool campaign_step_to_params(const campaign_config_t *cfg, uint32_t step,
                             uint32_t *out_delay,
                             uint32_t *out_width,
                             uint32_t *out_power) {
    if (cfg == NULL || out_delay == NULL || out_width == NULL
     || out_power == NULL) {
        return false;
    }
    uint32_t d = campaign_axis_step_count(&cfg->delay);
    uint32_t w = campaign_axis_step_count(&cfg->width);
    uint32_t p = campaign_axis_step_count(&cfg->power);
    if (d == 0u || w == 0u || p == 0u) return false;
    if (step >= d * w * p) return false;

    // Cartesian decomposition: power innermost, width middle, delay
    // outermost. step = (delay_idx * w + width_idx) * p + power_idx.
    uint32_t power_idx = step % p;
    uint32_t rest      = step / p;
    uint32_t width_idx = rest % w;
    uint32_t delay_idx = rest / w;

    *out_delay = axis_value_at(&cfg->delay, delay_idx);
    *out_width = axis_value_at(&cfg->width, width_idx);
    *out_power = axis_value_at(&cfg->power, power_idx);
    return true;
}

// -----------------------------------------------------------------------------
// Default executor — instant pass, no engine work
// -----------------------------------------------------------------------------

bool campaign_noop_executor(uint32_t step, const campaign_config_t *cfg,
                            uint32_t delay, uint32_t width, uint32_t power,
                            uint8_t *fire, uint8_t *verify, uint32_t *target,
                            void *user) {
    (void)step; (void)cfg; (void)delay; (void)width; (void)power; (void)user;
    if (fire   != NULL) *fire   = 0u;
    if (verify != NULL) *verify = 0u;
    if (target != NULL) *target = 0u;
    return true;
}

// -----------------------------------------------------------------------------
// Ringbuffer
// -----------------------------------------------------------------------------

static void ring_push(const campaign_result_t *r) {
    if (s_cm.ring_count >= CAMPAIGN_RESULT_RING_DEPTH) {
        // Full — drop. Host is lagging.
        s_cm.results_dropped++;
        return;
    }
    s_cm.ring[s_cm.ring_head] = *r;
    s_cm.ring_head = (s_cm.ring_head + 1u) % CAMPAIGN_RESULT_RING_DEPTH;
    s_cm.ring_count++;
    s_cm.results_pushed++;
}

size_t campaign_manager_drain_results(campaign_result_t *out, size_t max_n) {
    if (out == NULL || max_n == 0u) return 0u;
    size_t n = 0u;
    while (n < max_n && s_cm.ring_count > 0u) {
        out[n++] = s_cm.ring[s_cm.ring_tail];
        s_cm.ring_tail = (s_cm.ring_tail + 1u) % CAMPAIGN_RESULT_RING_DEPTH;
        s_cm.ring_count--;
    }
    return n;
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

void campaign_manager_init(void) {
    memset(&s_cm, 0, sizeof(s_cm));
    s_cm.state    = CAMPAIGN_STATE_IDLE;
    s_cm.executor = campaign_noop_executor;
}

bool campaign_manager_configure(const campaign_config_t *cfg) {
    if (cfg == NULL) {
        s_cm.err = CAMPAIGN_ERR_BAD_CONFIG;
        return false;
    }
    if (s_cm.state == CAMPAIGN_STATE_SWEEPING) {
        // Reconfiguring mid-sweep is a programmer error — refuse.
        s_cm.err = CAMPAIGN_ERR_BAD_CONFIG;
        return false;
    }
    uint32_t total = campaign_total_steps(cfg);
    if (total == 0u) {
        s_cm.err = CAMPAIGN_ERR_BAD_CONFIG;
        return false;
    }
    s_cm.cfg         = *cfg;
    s_cm.cfg_valid   = true;
    s_cm.total_steps = total;
    s_cm.step_n      = 0u;
    s_cm.state       = CAMPAIGN_STATE_CONFIGURING;
    s_cm.err         = CAMPAIGN_ERR_NONE;
    return true;
}

bool campaign_manager_start(void) {
    if (!s_cm.cfg_valid) {
        s_cm.err = CAMPAIGN_ERR_NOT_CONFIGURED;
        return false;
    }
    if (s_cm.state == CAMPAIGN_STATE_SWEEPING) {
        // Already running.
        return false;
    }
    s_cm.step_n          = 0u;
    s_cm.results_pushed  = 0u;
    s_cm.results_dropped = 0u;
    s_cm.ring_head       = 0u;
    s_cm.ring_tail       = 0u;
    s_cm.ring_count      = 0u;
    s_cm.last_step_at_ms = 0u;
    s_cm.state           = CAMPAIGN_STATE_SWEEPING;
    s_cm.err             = CAMPAIGN_ERR_NONE;
    return true;
}

void campaign_manager_stop(void) {
    if (s_cm.state == CAMPAIGN_STATE_SWEEPING) {
        s_cm.state = CAMPAIGN_STATE_STOPPED;
    }
}

void campaign_manager_tick(void) {
    if (s_cm.state != CAMPAIGN_STATE_SWEEPING) return;
    if (s_cm.step_n >= s_cm.total_steps) {
        s_cm.state = CAMPAIGN_STATE_DONE;
        return;
    }

    // Inter-step settle.
    if (s_cm.cfg.settle_ms > 0u && s_cm.last_step_at_ms != 0u) {
        uint32_t elapsed = (uint32_t)(hal_now_ms() - s_cm.last_step_at_ms);
        if (elapsed < s_cm.cfg.settle_ms) return;
    }

    uint32_t delay = 0u, width = 0u, power = 0u;
    if (!campaign_step_to_params(&s_cm.cfg, s_cm.step_n,
                                 &delay, &width, &power)) {
        s_cm.err   = CAMPAIGN_ERR_INTERNAL;
        s_cm.state = CAMPAIGN_STATE_ERROR;
        return;
    }

    uint8_t  fire_status   = 0u;
    uint8_t  verify_status = 0u;
    uint32_t target_state  = 0u;
    bool ok = (s_cm.executor != NULL)
            ? s_cm.executor(s_cm.step_n, &s_cm.cfg, delay, width, power,
                            &fire_status, &verify_status, &target_state,
                            s_cm.executor_user)
            : false;

    campaign_result_t r = {
        .step_n        = s_cm.step_n,
        .delay         = delay,
        .width         = width,
        .power         = power,
        .fire_status   = fire_status,
        .verify_status = verify_status,
        .target_state  = target_state,
        .ts_us         = hal_now_us(),
    };
    ring_push(&r);

    if (!ok) {
        // Step executor reported failure. Record the result, mark
        // ERROR, and stop. F9-3's adapter will surface engine-specific
        // status codes via `fire_status` so the host can diagnose.
        s_cm.err   = CAMPAIGN_ERR_STEP_FAILED;
        s_cm.state = CAMPAIGN_STATE_ERROR;
        return;
    }

    s_cm.step_n++;
    s_cm.last_step_at_ms = hal_now_ms();
    if (s_cm.step_n >= s_cm.total_steps) {
        s_cm.state = CAMPAIGN_STATE_DONE;
    }
}

void campaign_manager_get_status(campaign_status_t *out) {
    if (out == NULL) return;
    out->state           = s_cm.state;
    out->err             = s_cm.err;
    out->step_n          = s_cm.step_n;
    out->total_steps     = s_cm.total_steps;
    out->results_pushed  = s_cm.results_pushed;
    out->results_dropped = s_cm.results_dropped;
}

void campaign_manager_set_step_executor(campaign_step_executor_t fn, void *user) {
    s_cm.executor      = (fn != NULL) ? fn : campaign_noop_executor;
    s_cm.executor_user = user;
}

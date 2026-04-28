#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// services/campaign_manager — F9 sweep orchestrator. Walks a
// cartesian product over (delay, width, power) parameters, dispatches
// each step to a configurable engine executor, optionally calls a
// post-fire verify hook (F9-3 wires real SWD verify when F6 unblocks),
// and streams results into a host-drainable ringbuffer.
//
// F9-2 lands the state machine + sweep generator + ringbuffer with a
// no-op default step executor (instant success, no engine call).
// F9-3 wires real EMFI / crowbar integration via
// `campaign_manager_set_step_executor`.

#define CAMPAIGN_RESULT_RING_DEPTH  256u  // 24 B/entry × 256 = 6 KB ringbuffer

typedef enum {
    CAMPAIGN_ENGINE_EMFI    = 0,
    CAMPAIGN_ENGINE_CROWBAR = 1,
} campaign_engine_t;

typedef enum {
    CAMPAIGN_STATE_IDLE        = 0,
    CAMPAIGN_STATE_CONFIGURING = 1,   // config validated + stored
    CAMPAIGN_STATE_SWEEPING    = 2,
    CAMPAIGN_STATE_DONE        = 3,
    CAMPAIGN_STATE_STOPPED     = 4,
    CAMPAIGN_STATE_ERROR       = 5,
} campaign_state_t;

typedef enum {
    CAMPAIGN_ERR_NONE          = 0,
    CAMPAIGN_ERR_BAD_CONFIG    = 1,
    CAMPAIGN_ERR_NOT_CONFIGURED= 2,
    CAMPAIGN_ERR_BUS_BUSY      = 3,   // F9-3: swd_bus_acquire failed
    CAMPAIGN_ERR_STEP_FAILED   = 4,   // executor returned false
    CAMPAIGN_ERR_INTERNAL      = 5,
} campaign_err_t;

typedef struct {
    uint32_t start;
    uint32_t end;
    uint32_t step;     // step == 0 → axis collapses to a single value (`start`)
} campaign_axis_t;

typedef struct {
    campaign_engine_t engine;
    campaign_axis_t   delay;     // µs from trigger to fire
    campaign_axis_t   width;     // pulse width — units engine-specific (EMFI µs, crowbar ns)
    campaign_axis_t   power;     // engine-specific (EMFI: HV charge target; crowbar: LP/HP path)
    uint32_t          settle_ms; // wait between fires (0 = no wait)
} campaign_config_t;

// Step executor — invoked once per sweep step. Returns true if the
// fire path completed (fire_status / verify_status are filled by the
// executor for downstream record). Default = `campaign_noop_executor`
// which sets all-zero status and returns true (used by F9-2 tests
// before the engine wiring lands in F9-3).
typedef bool (*campaign_step_executor_t)(uint32_t step,
                                         const campaign_config_t *cfg,
                                         uint32_t delay,
                                         uint32_t width,
                                         uint32_t power,
                                         uint8_t *out_fire_status,
                                         uint8_t *out_verify_status,
                                         uint32_t *out_target_state,
                                         void *user);

// 28 B fixed-width result record. Plan §F9 D1 originally listed it
// as 24 B but the per-field math gives 28 once the reserved[2]
// slot for future ack flags is counted; 28 stays comfortably under
// most CDC frame sizes and aligns to a 4-byte boundary so the host
// parser can cast directly.
typedef struct {
    uint32_t step_n;
    uint32_t delay;
    uint32_t width;
    uint32_t power;
    uint8_t  fire_status;
    uint8_t  verify_status;
    uint8_t  reserved[2];
    uint32_t target_state;
    uint32_t ts_us;
} __attribute__((packed)) campaign_result_t;

typedef struct {
    campaign_state_t state;
    campaign_err_t   err;
    uint32_t         step_n;          // next step to run (or last completed +1 in DONE)
    uint32_t         total_steps;
    uint32_t         results_pushed;
    uint32_t         results_dropped; // ringbuffer overflow counter
} campaign_status_t;

void   campaign_manager_init(void);
bool   campaign_manager_configure(const campaign_config_t *cfg);
bool   campaign_manager_start(void);
void   campaign_manager_stop(void);
void   campaign_manager_tick(void);

void   campaign_manager_get_status(campaign_status_t *out);
size_t campaign_manager_drain_results(campaign_result_t *out, size_t max_n);

void   campaign_manager_set_step_executor(campaign_step_executor_t fn, void *user);

// Default no-op executor — declared here so tests / F9-3 init can
// reset to it explicitly.
bool   campaign_noop_executor(uint32_t step, const campaign_config_t *cfg,
                              uint32_t delay, uint32_t width, uint32_t power,
                              uint8_t *fire, uint8_t *verify, uint32_t *target,
                              void *user);

// -----------------------------------------------------------------------------
// Pure helpers — exposed for tests + F9-3 / F9-4.
// -----------------------------------------------------------------------------

// Step count along one axis. start..end inclusive, with step.
//   step == 0  → 1 (axis collapses to start)
//   start>end  → 0
uint32_t campaign_axis_step_count(const campaign_axis_t *axis);

// Total steps in the cartesian product. 0 if any axis is empty.
uint32_t campaign_total_steps(const campaign_config_t *cfg);

// Map a 0-based step index to (delay, width, power) values.
// Returns false on out-of-range or invalid config.
//
// Iteration order: power inner-most → width middle → delay outer-most
// (so the fastest sweep is over the most-fluctuating axis first). The
// emfi/crowbar campaigns each have their own physical ordering
// preferences; this is the default and F9-3 may override it.
bool campaign_step_to_params(const campaign_config_t *cfg, uint32_t step,
                             uint32_t *out_delay,
                             uint32_t *out_width,
                             uint32_t *out_power);

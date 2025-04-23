#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"

#include "faultier.pb.h"

typedef enum _GlitchOutput_t {
  GlitchOutput_None = 0,
  GlitchOutput_LP,
  GlitchOutput_HP,
} GlitchOutput_t;

struct glitcher_configuration {
  TriggersType trigger_type;
  TriggerPullConfiguration trigger_pull_configuration;
  GlitchOutput_t glitch_output;
  uint32_t delay_before_pulse;
  uint32_t pulse_width;
};

void glitcher_init();

/**
 * @brief Configure the glitcher with default params
 */
void glitcher_set_default_config();

/**
 * @brief Configure the glitcher with custom params
 * @param trigger_type The trigger type
 * @param glitch_output The glitch output
 * @param delay The delay before the pulse
 * @param pulse The pulse width
 */
void glitcher_set_config(TriggersType trigger_type, GlitchOutput_t glitch_output, uint32_t delay, uint32_t pulse);

/**
 * @brief Get the current glitcher configuration
 * @param config The configuration struct to fill
 */
void glitcher_get_config(struct glitcher_configuration* config);

bool glitcher_configure();

/**
 * @brief Execute the glitcher
 * @details This function will setup the glitcher and execute it
 *
 * @note This function will block until the glitch is done or timed out
 *
 * @return void
 */
void glitcher_run();

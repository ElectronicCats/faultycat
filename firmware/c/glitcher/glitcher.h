#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"

#include "faultier.pb.h"

#define GLITCHER_TRIGGER_PIN 8
#define GLITCHER_LP_GLITCH_PIN 16
#define GLITCHER_HP_GLITCH_PIN 17

typedef enum _GlitchOutput_t {
  GlitchOutput_LP = GLITCHER_LP_GLITCH_PIN,
  GlitchOutput_HP = GLITCHER_HP_GLITCH_PIN,
  GlitchOutput_None = 0,
} GlitchOutput_t;

struct glitcher_configuration {
  bool configured;
  TriggersType trigger_type;
  GlitchOutput_t glitch_output;
  uint32_t delay_before_pulse;
  uint32_t pulse_width;
};

void glitcher_init();
void glitcher_test_configure();
void glitcher_configure(TriggersType trigger_type, GlitchOutput_t glitch_output, uint32_t delay, uint32_t pulse);
bool glitcher_simple_setup();
void glitcher_run();

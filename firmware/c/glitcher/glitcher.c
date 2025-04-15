#include "glitcher.h"

#include "delay_compiler.h"
#include "ft_pio.h"
#include "glitch_compiler.h"
#include "power_cycler.h"
#include "trigger_compiler.h"

#include "delay_compiler.h"
#include "ft_pio.h"
#include "glitch_compiler.h"
#include "power_cycler.h"
#include "trigger_compiler.h"
#include "faultier.pb.h"
#include "faultier.h"

#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include <stdio.h>

#define GLITCHER_TRIGGER_PIN 8
#define GLITCHER_LP_GLITCH_PIN 16
#define GLITCHER_HP_GLITCH_PIN 17

struct glitcher_configuration {
  bool configured;
  GlitchOutput power_cycle_output;
  uint32_t power_cycle_length;
  TriggerSource trigger_source;
  TriggersType trigger_type;
  GlitchOutput glitch_output;
  uint32_t delay;
  uint32_t pulse;
  TriggerPullConfiguration trigger_pull_configuration;
};

struct glitcher_configuration glitcher = {
    .configured = false,
    .power_cycle_output = GlitchOutput_OUT_NONE,
    .power_cycle_length = 0,
    .trigger_source = TriggerSource_TRIGGER_IN_EXT1,
    .trigger_type = TriggersType_TRIGGER_NONE,
    .glitch_output = GlitchOutput_OUT_EXT0,
    .delay = 0,
    .pulse = 0,
    .trigger_pull_configuration = TriggerPullConfiguration_TRIGGER_PULL_NONE};

/**
 * @brief Configure the glitcher with default params for testing
 */
void glitcher_test_configure() {
  glitcher.configured = true;
  glitcher.power_cycle_output = GlitchOutput_OUT_EXT0;
  glitcher.power_cycle_length = 0;
  glitcher.trigger_source = TriggerSource_TRIGGER_IN_EXT1;
  glitcher.trigger_type = TriggersType_TRIGGER_RISING_EDGE;
  glitcher.glitch_output = GlitchOutput_OUT_EXT1;
  glitcher.delay = 1000;  // 1000 cycles
  glitcher.pulse = 100;   // 100 cycles
  glitcher.trigger_pull_configuration = TriggerPullConfiguration_TRIGGER_PULL_NONE;
}

bool glitcher_simple_setup() {
  struct ft_pio_program program = {0};
  ft_pio_program_init(&program);
  pio_sm_config c = pio_get_default_sm_config();

  if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
    uint power_cycle_pin = 0;
    // switch (glitcher.power_cycle_output) {
    //   case GlitchOutput_OUT_EXT0:
    //     power_cycle_pin = PIN_EXT0;
    //     break;
    //   case GlitchOutput_OUT_EXT1:
    //     power_cycle_pin = PIN_EXT1;
    //     break;
    //   case GlitchOutput_OUT_CROWBAR:
    //     power_cycle_pin = PIN_GATE;
    //     break;
    //   case GlitchOutput_OUT_MUX0:
    //     power_cycle_pin = PIN_MUX0;
    //     break;
    //   case GlitchOutput_OUT_MUX1:
    //     power_cycle_pin = PIN_MUX1;
    //     break;
    //   case GlitchOutput_OUT_MUX2:
    //     power_cycle_pin = PIN_MUX2;
    //     break;
    // }
    pio_gpio_init(pio0, power_cycle_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, power_cycle_pin, 1, true);
    sm_config_set_out_pins(&c, power_cycle_pin, 1);
    power_cycler(&program);
  }

  // Trigger (can be none/high/low/rising/falling)
  switch (glitcher.trigger_type) {
    case TriggersType_TRIGGER_NONE:
      break;
    case TriggersType_TRIGGER_HIGH:
      trigger_high(&program);
      break;
    case TriggersType_TRIGGER_LOW:
      trigger_low(&program);
      break;
    case TriggersType_TRIGGER_RISING_EDGE:
      trigger_rising(&program);
      break;
    case TriggersType_TRIGGER_FALLING_EDGE:
      trigger_falling(&program);
      break;
    case TriggersType_TRIGGER_PULSE_POSITIVE:
      trigger_pulse_positive(&program);
      break;
    case TriggersType_TRIGGER_PULSE_NEGATIVE:
      trigger_pulse_negative(&program);
      break;

    default:
      // TODO: Error
      return false;
      break;
  }

  // Trigger IRQ
  uint inst = pio_encode_irq_set(0, PIO_IRQ_TRIGGERED);
  ft_pio_program_add_inst(&program, inst);
  // Delay
  delay_regular(&program);

  // Glitcher
  if (glitcher.glitch_output != GlitchOutput_OUT_NONE) {
    glitcher_simple(&program);
  }

  // Post-glitch IRQ
  inst = pio_encode_irq_set(0, PIO_IRQ_GLITCHED);
  ft_pio_program_add_inst(&program, inst);

  ft_pio_add_program(&program);

  if (glitcher.trigger_type != TriggersType_TRIGGER_NONE) {
    // int trigger_pin = PIN_EXT1;
    int trigger_pin = GLITCHER_TRIGGER_PIN;
    // switch (glitcher.trigger_source) {
    //   case TriggerSource_TRIGGER_IN_EXT0:
    //     trigger_pin = PIN_EXT0;
    //     break;
    //   case TriggerSource_TRIGGER_IN_EXT1:
    //     trigger_pin = PIN_EXT1;
    //     break;
    //   default:
    //     break;
    // }

    // gpio_pull_up(trigger_pin);
    switch (glitcher.trigger_pull_configuration) {
      case TriggerPullConfiguration_TRIGGER_PULL_NONE:
        gpio_disable_pulls(trigger_pin);
        break;
      case TriggerPullConfiguration_TRIGGER_PULL_UP:
        gpio_pull_up(trigger_pin);
        break;
      case TriggerPullConfiguration_TRIGGER_PULL_DOWN:
        gpio_pull_down(trigger_pin);
        break;
    }

    sm_config_set_in_pins(&c, trigger_pin);
    pio_gpio_init(pio0, trigger_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, trigger_pin, 1, false);
  }

  if (glitcher.glitch_output != GlitchOutput_OUT_NONE) {
    // int glitch_pin = PIN_GATE;
    int glitch_pin = GLITCHER_HP_GLITCH_PIN;
    // switch (glitcher.glitch_output) {
    //   case GlitchOutput_OUT_EXT0:
    //     glitch_pin = PIN_EXT0;
    //     break;
    //   case GlitchOutput_OUT_EXT1:
    //     glitch_pin = PIN_EXT1;
    //     break;
    //   case GlitchOutput_OUT_CROWBAR:
    //     glitch_pin = PIN_GATE;
    //     break;
    //   case GlitchOutput_OUT_MUX0:
    //     glitch_pin = PIN_MUX0;
    //     break;
    //   case GlitchOutput_OUT_MUX1:
    //     glitch_pin = PIN_MUX1;
    //     break;
    //   case GlitchOutput_OUT_MUX2:
    //     glitch_pin = PIN_MUX2;
    //     break;
    // }

    sm_config_set_set_pins(&c, glitch_pin, 1);
    pio_gpio_init(pio0, glitch_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, glitch_pin, 1, true);
  }

  pio_sm_init(pio0, 0, program.loaded_offset, &c);
  pio_sm_set_enabled(pio0, 0, true);

  printf("Glitcher configured successfully\n");
  return true;
}

/**
 * @brief Run the glitcher with the given parameters - do not call this
 * @param delay Delay in cycles
 * @param pulse_width Pulse width in cycles
 * @param trigger_pin Trigger pin number
 * @param glitch_pin Glitch pin number
 * @return true if successful, false otherwise
 */
bool glitcher_simple_run(uint delay, uint pulse_width, int trigger_pin, int glitch_pin) {
  // Initialize PIO program
  struct ft_pio_program program = {0};
  ft_pio_program_init(&program);
  pio_sm_config c = pio_get_default_sm_config();

  // Configure trigger input
  gpio_init(trigger_pin);
  gpio_set_dir(trigger_pin, GPIO_IN);
  sm_config_set_in_pins(&c, trigger_pin);
  pio_gpio_init(pio0, trigger_pin);
  pio_sm_set_consecutive_pindirs(pio0, 0, trigger_pin, 1, false);

  // Configure glitch output
  gpio_init(glitch_pin);
  gpio_set_dir(glitch_pin, GPIO_OUT);
  sm_config_set_set_pins(&c, glitch_pin, 1);
  pio_gpio_init(pio0, glitch_pin);
  pio_sm_set_consecutive_pindirs(pio0, 0, glitch_pin, 1, true);

  // Setup trigger detection (rising edge)
  trigger_rising(&program);

  // Add trigger IRQ
  uint inst = pio_encode_irq_set(0, PIO_IRQ_TRIGGERED);
  ft_pio_program_add_inst(&program, inst);

  // Add delay
  delay_regular(&program);

  // Add glitch pulse
  glitcher_simple(&program);

  // Add completion IRQ
  inst = pio_encode_irq_set(0, PIO_IRQ_GLITCHED);
  ft_pio_program_add_inst(&program, inst);

  // Load program
  ft_pio_add_program(&program);

  pio_sm_init(pio0, 0, program.loaded_offset, &c);

  pio_sm_set_enabled(pio0, 0, true);

  // Set delay and pulse width
  pio_sm_put_blocking(pio0, 0, delay);
  pio_sm_put_blocking(pio0, 0, pulse_width);

  return true;
}
#include <stdio.h>

#include "glitcher_commands.h"
#include "serial_utils.h"

void print_trigger_type(TriggersType trigger_type) {
  switch (trigger_type) {
    case TriggersType_TRIGGER_NONE:
      printf("None\n");
      break;
    case TriggersType_TRIGGER_HIGH:
      printf("High\n");
      break;
    case TriggersType_TRIGGER_LOW:
      printf("Low\n");
      break;
    case TriggersType_TRIGGER_RISING_EDGE:
      printf("Rising Edge\n");
      break;
    case TriggersType_TRIGGER_FALLING_EDGE:
      printf("Falling Edge\n");
      break;
    case TriggersType_TRIGGER_PULSE_POSITIVE:
      printf("Pulse Positive\n");
      break;
    case TriggersType_TRIGGER_PULSE_NEGATIVE:
      printf("Pulse Negative\n");
      break;
    case TriggersType_TRIGGER_SERIAL:
      printf("Serial (HW UART)\n");
      break;

    default:
      printf("Unknown\n");
  }
}

void print_trigger_pull_configuration(TriggerPullConfiguration trigger_pull_configuration) {
  switch (trigger_pull_configuration) {
    case TriggerPullConfiguration_TRIGGER_PULL_NONE:
      printf("None\n");
      break;
    case TriggerPullConfiguration_TRIGGER_PULL_UP:
      printf("Pull Up\n");
      break;
    case TriggerPullConfiguration_TRIGGER_PULL_DOWN:
      printf("Pull Down\n");
      break;
    default:
      printf("Unknown\n");
  }
}

void print_glitch_output(GlitchOutput_t glitch_output) {
  switch (glitch_output) {
    case GlitchOutput_LP:
      printf("LP\n");
      break;
    case GlitchOutput_HP:
      printf("HP\n");
      break;
    case GlitchOutput_EMP:
      printf("EMP (Antenna)\n");
      break;
    case GlitchOutput_None:
      printf("None\n");
      break;
    default:
      printf("Unknown\n");
  }
}

void glitcher_commands_get_config() {
  struct glitcher_configuration config;
  glitcher_get_config(&config);
  printf("Glitcher configuration status:\n");
  printf("- Trigger type: ");
  print_trigger_type(config.trigger_type);
  printf("- Trigger pull configuration: ");
  print_trigger_pull_configuration(config.trigger_pull_configuration);
  printf("- Glitch output: ");
  print_glitch_output(config.glitch_output);
  printf("- Delay before pulse: %d cycles\n", config.delay_before_pulse);
  printf("- Pulse width: %d cycles\n", config.pulse_width);
  if (config.trigger_type == TriggersType_TRIGGER_SERIAL) {
      printf("- HW Serial pattern: \"%s\" on GP%d at %d baud\n", glitcher.serial_pattern, glitcher.serial_pin, glitcher.serial_baud);
  }
}

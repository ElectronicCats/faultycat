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
    case GlitchOutput_None:
      printf("None\n");
      break;
    default:
      printf("Unknown\n");
  }
}

void glitcher_commands_configure() {
  printf("     Enter trigger type (0-6):\n");
  printf("     0: None\n");
  printf("     1: High\n");
  printf("     2: Low\n");
  printf("     3: Rising Edge\n");
  printf("     4: Falling Edge\n");
  printf("     5: Pulse Positive\n");
  printf("     6: Pulse Negative\n");
  printf("     > ");
  
  int32_t val;
  while (!safe_read_int(&val, 1) || val < 0 || val > 6) {
    printf("     Invalid. Enter (0-6): ");
  }
  TriggersType trigger_type = (TriggersType)val;
  printf("     Trigger type set to: %d\n", val);
  print_trigger_type(trigger_type);

  printf("     Enter trigger pull configuration (0-2):\n");
  printf("     0: None\n");
  printf("     1: Pull Up\n");
  printf("     2: Pull Down\n");
  printf("     > ");
  
  while (!safe_read_int(&val, 1) || val < 0 || val > 2) {
    printf("     Invalid. Enter (0-2): ");
  }
  TriggerPullConfiguration trigger_pull_configuration = (TriggerPullConfiguration)val;
  printf("     Trigger pull configuration set to: %d\n", val);
  print_trigger_pull_configuration(trigger_pull_configuration);

  printf("     Enter glitch output (0-2):\n");
  printf("     0: None\n");
  printf("     1: LP\n");
  printf("     2: HP\n");
  printf("     > ");
  
  while (!safe_read_int(&val, 1) || val < 0 || val > 2) {
      printf("     Invalid. Enter (0-2): ");
  }
  GlitchOutput_t glitch_output = (GlitchOutput_t)val;
  printf("     Glitch output set to: %d\n", val);
  print_glitch_output(glitch_output);

  printf("     Enter delay before pulse (in cycles): ");
  int32_t delay_val;
  while (!safe_read_int(&delay_val, 10) || delay_val < 0) {
    printf("     Invalid value. Try again: ");
  }
  printf("     Delay before pulse set to: %d cycles\n", delay_val);

  printf("     Enter pulse width (in cycles): ");
  int32_t width_val;
  // Prevent 0 width which causes infinite loop in PIO
  while (!safe_read_int(&width_val, 10) || width_val <= 0) {
    printf("     Invalid value (must be > 0). Try again: ");
  }
  printf("     Pulse width set to: %d cycles\n", width_val);
  
  printf("     Configuring glitcher...\n");
  glitcher_set_config(trigger_type, glitch_output, (uint32_t)delay_val, (uint32_t)width_val);
  printf("     Glitcher configured successfully\n");
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
}

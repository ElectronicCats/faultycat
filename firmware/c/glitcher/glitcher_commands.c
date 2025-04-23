#include <stdio.h>

#include "glitcher_commands.h"
#include "glitcher.h"
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
  int trigger_type_int = getIntFromSerial();
  while (trigger_type_int < 0 || trigger_type_int > 6) {
    printf("     Enter a valid value: ");
    trigger_type_int = getIntFromSerial();
  }
  TriggersType trigger_type = (TriggersType)trigger_type_int;
  printf("     Trigger type set to: ", trigger_type_int);
  print_trigger_type(trigger_type);

  printf("     Enter glitch output (0-2):\n");
  printf("     0: None\n");
  printf("     1: LP\n");
  printf("     2: HP\n");
  printf("     > ");
  int glitch_output_int = getIntFromSerial();
  while (glitch_output_int < 0 || glitch_output_int > 2) {
    printf("     Enter a valid value: ");
    glitch_output_int = getIntFromSerial();
  }
  GlitchOutput_t glitch_output = (GlitchOutput_t)glitch_output_int;
  printf("     Glitch output set to: ", glitch_output_int);
  print_glitch_output(glitch_output);

  printf("     Enter delay before pulse (in cycles): ");
  int delay_before_pulse = getIntFromSerial();
  while (delay_before_pulse < 0) {
    printf("     Enter a valid value: ");
    delay_before_pulse = getIntFromSerial();
  }
  printf("     Delay before pulse set to: %d cycles\n", delay_before_pulse);

  printf("     Enter pulse width (in cycles): ");
  int pulse_width = getIntFromSerial();
  while (pulse_width < 0) {
    printf("     Enter a valid value: ");
    pulse_width = getIntFromSerial();
  }
  printf("     Pulse width set to: %d cycles\n", pulse_width);
  printf("     Configuring glitcher...\n");
  glitcher_configure(trigger_type, glitch_output, delay_before_pulse, pulse_width);
  printf("     Glitcher configured successfully\n");
}

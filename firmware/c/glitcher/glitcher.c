#include "glitcher.h"

#include "delay_compiler.h"
#include "ft_pio.h"
#include "glitch_compiler.h"
#include "trigger_compiler.h"

#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"

#define PIN_LED1 10
#define PIN_LED2 9

// ADC channels
#define ADC_MUX_PIN 26
#define ADC_EXT_PIN 27
#define ADC_CB_PIN 28
#define ADC_MUX 0
#define ADC_EXT 1
#define ADC_CB 2

#define PIO_IRQ_TRIGGERED 0
#define PIO_IRQ_GLITCHED 1

struct glitcher_configuration glitcher = {
    .configured = false,
    .trigger_type = TriggersType_TRIGGER_NONE,
    .glitch_output = GlitchOutput_LP,
    .delay_before_pulse = 0,
    .pulse_width = 0};

void glitcher_init() {
  // Initialize GPIO pins
  gpio_init(PIN_LED1);
  gpio_set_dir(PIN_LED1, GPIO_OUT);
  gpio_put(PIN_LED1, 0);

  gpio_init(PIN_LED2);
  gpio_set_dir(PIN_LED2, GPIO_OUT);
  gpio_put(PIN_LED2, 0);

  // Initialize ADC pins
  // gpio_init(ADC_MUX_PIN);
  // gpio_set_dir(ADC_MUX_PIN, GPIO_IN);

  // gpio_init(ADC_EXT_PIN);
  // gpio_set_dir(ADC_EXT_PIN, GPIO_IN);

  // gpio_init(ADC_CB_PIN);
  // gpio_set_dir(ADC_CB_PIN, GPIO_IN);
}

/**
 * @brief Configure the glitcher with default params for testing
 */
void glitcher_test_configure() {
  glitcher.configured = true;
  glitcher.trigger_type = TriggersType_TRIGGER_RISING_EDGE;
  glitcher.glitch_output = GlitchOutput_OUT_EXT1;
  glitcher.delay_before_pulse = 1000;  // 1000 cycles
  glitcher.pulse_width = 2500;  // 10 us (2500 cycles at 250MHz)
}

void glitcher_configure(TriggersType trigger_type, GlitchOutput_t glitch_output, uint32_t delay, uint32_t pulse) {
  glitcher.configured = true;
  glitcher.trigger_type = trigger_type;
  glitcher.glitch_output = glitch_output;
  glitcher.delay_before_pulse = delay;
  glitcher.pulse_width = pulse;
}

bool glitcher_simple_setup() {
  struct ft_pio_program program = {0};
  ft_pio_program_init(&program);
  pio_sm_config c = pio_get_default_sm_config();

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
  if (glitcher.glitch_output != GlitchOutput_None) {
    glitcher_simple(&program);
  }

  // Post-glitch IRQ
  inst = pio_encode_irq_set(0, PIO_IRQ_GLITCHED);
  ft_pio_program_add_inst(&program, inst);

  ft_pio_add_program(&program);

  // Configure trigger input
  gpio_init(GLITCHER_TRIGGER_PIN);
  gpio_pull_down(GLITCHER_TRIGGER_PIN);
  sm_config_set_in_pins(&c, GLITCHER_TRIGGER_PIN);
  pio_gpio_init(pio0, GLITCHER_TRIGGER_PIN);
  pio_sm_set_consecutive_pindirs(pio0, 0, GLITCHER_TRIGGER_PIN, 1, false);

  if (glitcher.glitch_output != GlitchOutput_None) {
    sm_config_set_set_pins(&c, GLITCHER_LP_GLITCH_PIN, GPIO_OUT);
    pio_gpio_init(pio0, GLITCHER_LP_GLITCH_PIN);
    pio_sm_set_consecutive_pindirs(pio0, 0, GLITCHER_LP_GLITCH_PIN, 1, true);
  }

  pio_sm_init(pio0, 0, program.loaded_offset, &c);
  pio_sm_set_enabled(pio0, 0, true);

  printf("Glitcher configured successfully\n");
  return true;
}

void prepare_adc() {
}

void capture_adc() {
}

void glitcher_run() {
  pio_clear_instruction_memory(pio0);
  glitcher_simple_setup();

  // Ready LED
  gpio_put(PIN_LED1, 1);

  // delay
  pio_sm_put_blocking(pio0, 0, glitcher.delay_before_pulse);

  prepare_adc();
  // pulse
  if (glitcher.glitch_output != GlitchOutput_None) {
    printf("Glitching...\n");
    pio_sm_put_blocking(pio0, 0, glitcher.pulse_width);
  }

  // Trigger timeout. Ca. 1 second currently.
  uint trigger_start_millis = to_ms_since_boot(get_absolute_time());
  bool trigger_timed_out = false;
  while (!pio_interrupt_get(pio0, PIO_IRQ_TRIGGERED) && !trigger_timed_out) {
    // Make sure UART bridge keeps working while glitching
    // uart_task();
    if ((to_ms_since_boot(get_absolute_time()) - trigger_start_millis) > 10000) {
      trigger_timed_out = true;
      break;
    }
  }
  if (trigger_timed_out) {
    pio_sm_set_enabled(pio0, 0, false);
    // There is a tiny chance of a race condition here that we check:
    // Even though the trigger timed out we might - in that microsecond -
    // had a successful trigger. So we quickly check whether the IRQ is set.
    if (!pio_interrupt_get(pio0, PIO_IRQ_TRIGGERED)) {
      pio_interrupt_clear(pio0, PIO_IRQ_TRIGGERED);
      pio_interrupt_clear(pio0, PIO_IRQ_GLITCHED);

      // TODO: implement something similar
      // protocol_trigger_timeout();
      pio_clear_instruction_memory(pio0);
      // TODO: Ensure all pins are back to default
      gpio_put(PIN_LED1, 0);
      gpio_put(PIN_LED2, 0);
      printf("Trigger timed out\n");
      // return;
    }
  } else {
    printf("Trigger successful\n");
  }

  capture_adc();

  pio_interrupt_clear(pio0, PIO_IRQ_TRIGGERED);
  // MISC LED
  gpio_put(PIN_LED2, 1);
  // TODO: Fix this area:
  //       - add error reporting for "glitch timeout"
  //       - run uart_task while glitching

  // Hardcoded 3 second timeout for glitcher, no error reporting
  // pio_interrupt_get_timeout_us(pio0, PIO_IRQ_GLITCHED, 3000000);  // TODO: should it be here?
  pio_interrupt_clear(pio0, PIO_IRQ_GLITCHED);
  pio_sm_set_enabled(pio0, 0, false);
  pio_clear_instruction_memory(pio0);
  gpio_put(PIN_LED1, 0);
  gpio_put(PIN_LED2, 0);
}

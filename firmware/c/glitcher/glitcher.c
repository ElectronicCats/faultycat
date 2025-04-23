#include "glitcher.h"

#include "delay_compiler.h"
#include "faultier.pb.h"
#include "ft_pio.h"
#include "glitch_compiler.h"
#include "power_cycler.h"
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
  uint32_t delay;
  uint32_t pulse;
  TriggerPullConfiguration trigger_pull_configuration;
};

struct glitcher_configuration glitcher = {
    .configured = false,
    .trigger_type = TriggersType_TRIGGER_NONE,
    .glitch_output = GlitchOutput_LP,
    .delay = 0,
    .pulse = 0,
    .trigger_pull_configuration = TriggerPullConfiguration_TRIGGER_PULL_NONE};

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
  glitcher.delay = 1000;  // 1000 cycles
  // glitcher.pulse = 100;   // 100 cycles
  glitcher.pulse = 2500;  // 10 us (2500 cycles at 250MHz)
  glitcher.trigger_pull_configuration = TriggerPullConfiguration_TRIGGER_PULL_DOWN;
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

/**
 * @brief Create a square wave on the glitch pin
 *
 * @param glitch_pin Pin to use for the glitch
 * @param glitch_width Width of the glitch in cycles
 */
void glitch_test(int glitch_pin, int glitch_width) {
  gpio_init(glitch_pin);
  gpio_set_dir(glitch_pin, GPIO_OUT);

  while (1) {
    gpio_put(glitch_pin, 1);
    sleep_us(glitch_width);
    gpio_put(glitch_pin, 0);
    sleep_us(glitch_width);
  }
}

void glitcher_run() {
  pio_clear_instruction_memory(pio0);
  glitcher_simple_setup();

  // Ready LED
  gpio_put(PIN_LED1, 1);

  // delay
  pio_sm_put_blocking(pio0, 0, glitcher.delay);

  prepare_adc();
  // pulse
  if (glitcher.glitch_output != GlitchOutput_None) {
    printf("Glitching...\n");
    pio_sm_put_blocking(pio0, 0, glitcher.pulse);
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
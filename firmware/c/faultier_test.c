#include "faultier_test.h"

#define PIO_IRQ_TRIGGERED 0
#define PIO_IRQ_GLITCHED 1

bool simple_glitch_run(uint delay, uint pulse_width, int trigger_pin, int glitch_pin) {
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

  // Wait for trigger with timeout
  uint32_t start = time_us_32();
  bool triggered = false;
  while (!pio_interrupt_get(pio0, PIO_IRQ_TRIGGERED)) {
    if (time_us_32() - start > 1000000) {  // 1 second timeout
      pio_sm_set_enabled(pio0, 0, false);
      pio_clear_instruction_memory(pio0);
      return false;
    }
  }

  // Wait for glitch completion
  start = time_us_32();
  while (!pio_interrupt_get(pio0, PIO_IRQ_GLITCHED)) {
    if (time_us_32() - start > 3000000) {  // 3 second timeout
      pio_sm_set_enabled(pio0, 0, false);
      pio_clear_instruction_memory(pio0);
      return false;
    }
  }

  // Cleanup
  pio_interrupt_clear(pio0, PIO_IRQ_TRIGGERED);
  pio_interrupt_clear(pio0, PIO_IRQ_GLITCHED);
  pio_sm_set_enabled(pio0, 0, false);
  pio_clear_instruction_memory(pio0);

  return true;
}
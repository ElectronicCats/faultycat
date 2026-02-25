#include "glitcher.h"

#include "delay_compiler.h"
#include "ft_pio.h"
#include "glitch_compiler.h"
#include "trigger_compiler.h"
#include "power_cycler.h"
#include "tusb.h"

#include <stdio.h>
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "picoemp.h"
#include "hardware/uart.h"
#include "pico/time.h"
#include "board_config.h"

// Original code had PIN_LED1 10, PIN_LED2 9. 
// Let's defer LED changes to avoid behavior change, but unify trigger/glitch pins.

#define PIN_LED1 10
// #define PIN_LED2 9 // Conflict with PIN_LED_HV in board_config.h

#ifndef PIN_EXT0
#define PIN_EXT0 4
#define PIN_EXT1 27
#define PIN_MUX0 1
#define PIN_MUX1 2
#define PIN_MUX2 3
#endif

#define ADC_DMA_CHANNEL 0
#define ADC_PIN PIN_ADC_INPUT
#define ADC_CHANNEL ADC_CHANNEL_NUM

#define PIO_IRQ_TRIGGERED 0
#define PIO_IRQ_GLITCHED 1

#define GLITCHER_TRIGGER_PIN PIN_TRIGGER
#define GLITCHER_LP_GLITCH_PIN PIN_GLITCH_LP
#define GLITCHER_HP_GLITCH_PIN PIN_GLITCH_HP

struct glitcher_configuration glitcher = {
    .trigger_type = TriggersType_TRIGGER_NONE,
    .trigger_pull_configuration = TriggerPullConfiguration_TRIGGER_PULL_NONE,
    .glitch_output = GlitchOutput_None,
    .delay_before_pulse = 0,
    .pulse_width = 0};

static uint8_t capture_buffer[CAPTURE_DEPTH];
static uint32_t sample_count = 1000;  // Default sample count

void glitcher_init() {
  // Initialize GPIO pins
  gpio_init(PIN_LED1);
  gpio_set_dir(PIN_LED1, GPIO_OUT);
  gpio_put(PIN_LED1, 0);

  // gpio_init(PIN_LED2);
  // gpio_set_dir(PIN_LED2, GPIO_OUT);
  // gpio_put(PIN_LED2, 0);

  // Initialize ADC pins
  // gpio_init(ADC_MUX_PIN);
  // gpio_set_dir(ADC_MUX_PIN, GPIO_IN);

  // gpio_init(ADC_EXT_PIN);
  // gpio_set_dir(ADC_EXT_PIN, GPIO_IN);

  // gpio_init(ADC_CB_PIN);
  // gpio_set_dir(ADC_CB_PIN, GPIO_IN);

  glitcher_set_default_config();
}
void glitcher_set_default_config() {
  glitcher_set_config(TriggersType_TRIGGER_RISING_EDGE, GlitchOutput_LP, 1000, 2500);
}

void glitcher_set_config(TriggersType trigger_type, GlitchOutput_t glitch_output, uint32_t delay, uint32_t pulse) {
  glitcher.trigger_type = trigger_type;
  glitcher.glitch_output = glitch_output;
  glitcher.delay_before_pulse = delay;
  glitcher.pulse_width = pulse;
}

void glitcher_get_config(struct glitcher_configuration* config) {
  config->trigger_type = glitcher.trigger_type;
  config->trigger_pull_configuration = glitcher.trigger_pull_configuration;
  config->glitch_output = glitcher.glitch_output;
  config->delay_before_pulse = glitcher.delay_before_pulse;
  config->pulse_width = glitcher.pulse_width;
}

// Global static to track the loaded PIO program
static struct ft_pio_program current_program = {0};

bool glitcher_configure() {
  struct ft_pio_program *program = &current_program;

  if (current_program.loaded) ft_pio_remove_program(&current_program);
  
  // Clean up ALL previous programs to avoid memory leak or collisions with fast_trigger
  pio_clear_instruction_memory(pio0);

  ft_pio_program_init(program);
  pio_sm_config c = pio_get_default_sm_config();

  // Trigger (can be none/high/low/rising/falling/serial)
  switch (glitcher.trigger_type) {
    case TriggersType_TRIGGER_NONE:
      break;
    case TriggersType_TRIGGER_HIGH:
      trigger_high(program);
      break;
    case TriggersType_TRIGGER_LOW:
      trigger_low(program);
      break;
    case TriggersType_TRIGGER_RISING_EDGE:
      trigger_rising(program);
      break;
    case TriggersType_TRIGGER_FALLING_EDGE:
      trigger_falling(program);
      break;
    case TriggersType_TRIGGER_PULSE_POSITIVE:
      trigger_pulse_positive(program);
      break;
    case TriggersType_TRIGGER_PULSE_NEGATIVE:
      trigger_pulse_negative(program);
      break;
    case TriggersType_TRIGGER_SERIAL:
      // HW UART doesn't need a hardware trigger program. 
      // Emulate a trigger by waiting for a CPU manual push via FIFO.
      {
         uint16_t instr = pio_encode_pull(false, true); // wait for CPU push
         ft_pio_program_add_inst(program, instr);
      }
      break;

    default:
      printf("Error: Invalid trigger type selected!\n");
      if (program->loaded) ft_pio_remove_program(program);
      return false;
      break;
  }

  // Trigger IRQ
  uint inst = pio_encode_irq_set(0, PIO_IRQ_TRIGGERED);
  ft_pio_program_add_inst(program, inst);

  // CPU must know immediately trigger happened, wait let's place power_cycler here
  if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
      uint power_cycle_pin = 0;
      switch(glitcher.power_cycle_output) {
          case GlitchOutput_OUT_EXT0: power_cycle_pin = PIN_EXT0; break;
          case GlitchOutput_OUT_EXT1: power_cycle_pin = PIN_EXT1; break;
          case GlitchOutput_OUT_CROWBAR: power_cycle_pin = 0; break; // PIN_GATE
          case GlitchOutput_OUT_MUX0: power_cycle_pin = PIN_MUX0; break;
          case GlitchOutput_OUT_MUX1: power_cycle_pin = PIN_MUX1; break;
          case GlitchOutput_OUT_MUX2: power_cycle_pin = PIN_MUX2; break;
          case GlitchOutput_OUT_EMP: power_cycle_pin = PIN_HV_PULSE; break;
          default: power_cycle_pin = 0; break;
      }
      pio_gpio_init(pio0, power_cycle_pin);
      pio_sm_set_consecutive_pindirs(pio0, 0, power_cycle_pin, 1, true);
      sm_config_set_out_pins(&c, power_cycle_pin, 1);
      
      power_cycler(program);
  }

  // Delay
  delay_regular(program);

  // Glitcher
  if (glitcher.glitch_output != GlitchOutput_None) {
    glitcher_simple(program);
  }

  // Post-glitch IRQ
  inst = pio_encode_irq_set(0, PIO_IRQ_GLITCHED);
  ft_pio_program_add_inst(program, inst);

  if (!ft_pio_add_program(program)) {
      printf("Error: PIO instruction memory full!\n");
      return false;
  }

  // Removed DEBUG print of assembled PIO program

  // Configure trigger input
  if (glitcher.trigger_type != TriggersType_TRIGGER_NONE) {
    int trigger_pin = GLITCHER_TRIGGER_PIN;

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
    
    pio_gpio_init(pio0, trigger_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, trigger_pin, 1, false);

    // Always map the IN pin to the SM config so `wait` instructions don't crash
    sm_config_set_in_pins(&c, trigger_pin);
  }

  // If glitch output is HP type
  if(glitcher.glitch_output == GlitchOutput_HP) {
    sm_config_set_set_pins(&c, GLITCHER_HP_GLITCH_PIN, GPIO_OUT);
    pio_gpio_init(pio0, GLITCHER_HP_GLITCH_PIN);
    pio_sm_set_consecutive_pindirs(pio0, 0, GLITCHER_HP_GLITCH_PIN, 1, true);
  }

  // If glitch output is LP type
  if(glitcher.glitch_output == GlitchOutput_LP) {
    sm_config_set_set_pins(&c, GLITCHER_LP_GLITCH_PIN, GPIO_OUT);
    pio_gpio_init(pio0, GLITCHER_LP_GLITCH_PIN);
    pio_sm_set_consecutive_pindirs(pio0, 0, GLITCHER_LP_GLITCH_PIN, 1, true);
  }

  // If glitch output is EMP type
  if(glitcher.glitch_output == GlitchOutput_EMP) {
    sm_config_set_set_pins(&c, PIN_HV_PULSE, GPIO_OUT);
    pio_gpio_init(pio0, PIN_HV_PULSE);
    pio_sm_set_consecutive_pindirs(pio0, 0, PIN_HV_PULSE, 1, true);
  }

  pio_sm_init(pio0, 0, program->loaded_offset, &c);
  pio_sm_set_enabled(pio0, 0, true);

  printf("Glitcher configured successfully\n");
  return true;
}

bool glitcher_set_adc_sample_count(uint32_t count) {
  if (count > CAPTURE_DEPTH) {
    printf("Sample count exceeds 30000 buffer size\n");
    return false;
  }
  sample_count = count;
  return true;
}

uint8_t* adc_get_capture_buffer() {
  return capture_buffer;
}

uint32_t adc_get_sample_count() {
  return sample_count;
}

void prepare_adc() {
  // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer
  adc_gpio_init(ADC_PIN);

  // Initialize ADC
  adc_init();
  adc_select_input(ADC_CHANNEL);

  // Setup ADC FIFO
  adc_fifo_setup(
      true,   // Write each completed conversion to the sample FIFO
      true,   // Enable DMA data request (DREQ)
      1,      // DREQ (and IRQ) asserted when at least 1 sample present
      false,  // We won't see the ERR bit because of 8 bit reads; disable.
      true    // Shift each sample to 8 bits when pushing to FIFO
  );

  // Set full speed (no clock divider)
  adc_set_clkdiv(0);

  // Configure DMA to capture ADC samples
  dma_channel_config cfg = dma_channel_get_default_config(ADC_DMA_CHANNEL);
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);
  
  // Important: Set it as a Ring Buffer wrapping on Write! 13 bits = 8192 bytes
  channel_config_set_ring(&cfg, true, 13);
  
  channel_config_set_dreq(&cfg, DREQ_ADC);

  dma_channel_configure(ADC_DMA_CHANNEL, &cfg,
                        capture_buffer,  // dst
                        &adc_hw->fifo,   // src
                        0xFFFFFFFF,      // Transfer count (infinite loop until aborted)
                        true             // start immediately
  );
}

void capture_adc() {
  // We aren't using this blocking function anymore.
  // The run loop will handle starting and stopping the ADC DMA directly.
}

static inline bool pio_interrupt_get_timeout_us(PIO pio, uint irq, uint timeout) {
  uint32_t start_time = time_us_32();
  while (!pio_interrupt_get(pio, irq)) {
    if ((time_us_32() - start_time) > timeout) {
      return false;
    }
  }
  return true;
}

bool glitcher_run() {
  if (glitcher.pulse_width == 0 && glitcher.glitch_output != GlitchOutput_None) {
      printf("Error: Pulse width is 0! Aborting to prevent PIO freeze.\n");
      return false;
  }

  // Ensure previous configuration is cleared
  if (!glitcher_configure()) {
    printf("Glitcher configuration failed\n");
    return false;
  }

  // Ready LEDs (Turn on both HV_ARMED and STA to indicate waiting)
  gpio_put(PIN_LED1, 1);

  prepare_adc();

  // Wait for Trigger (with 3-second timeout for normal, indefinite for serial)
  adc_run(true);

  // Push regular parameters (Power Cycler, Delay, Pulse Width)
  // These are handled by PULL instructions at addresses 0, 5, and 7.
  // We must push these BEFORE the trigger wait because the PIO program
  // executes setup and THEN waits for the trigger.
  if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
     pio_sm_put_blocking(pio0, 0, glitcher.power_cycle_length);
  }
  
  pio_sm_put_blocking(pio0, 0, glitcher.delay_before_pulse);
  
  if (glitcher.glitch_output != GlitchOutput_None) {
      pio_sm_put_blocking(pio0, 0, glitcher.pulse_width);
  }

  uint32_t start_time = time_us_32();
  bool trigger_timeout = false;
  
  if (glitcher.trigger_type == TriggersType_TRIGGER_SERIAL) {
      // Determine UART instance based on pin
      uart_inst_t *uart_inst = (glitcher.serial_pin == 1) ? uart0 : uart1;
      
      uart_init(uart_inst, glitcher.serial_baud);
      gpio_set_function(glitcher.serial_pin, GPIO_FUNC_UART);
      uart_set_hw_flow(uart_inst, false, false);
      uart_set_format(uart_inst, 8, 1, UART_PARITY_NONE);
      uart_set_fifo_enabled(uart_inst, true);
      gpio_pull_up(glitcher.serial_pin);
      
      // Flush RX FIFO
      while (uart_is_readable(uart_inst)) {
          uart_getc(uart_inst);
      }
      
      glitcher.serial_pattern[sizeof(glitcher.serial_pattern) - 1] = '\0';
      uint32_t pattern_len = strlen(glitcher.serial_pattern);
      uint32_t match_idx = 0;
      uint32_t last_print = 0;
      uint32_t toggle_count = 0;
      bool last_gpio_state = gpio_get(glitcher.serial_pin);
      
      printf("Waiting for serial pattern \"%s\" on GP%d (%d baud)...\n", glitcher.serial_pattern, glitcher.serial_pin, glitcher.serial_baud);
      printf("(Monitoring GP%d for any electrical activity...)\n", glitcher.serial_pin);
      
      // Ensure pulse button is initialized for manual override
      gpio_init(PIN_BTN_PULSE);
      gpio_set_dir(PIN_BTN_PULSE, GPIO_IN);
      gpio_set_pulls(PIN_BTN_PULSE, true, false);
      gpio_set_inover(PIN_BTN_PULSE, GPIO_OVERRIDE_INVERT);

      while (true) {
          tud_task();
          
          picoemp_process_charging();

          // Electrical Signal Monitor
          bool current_gpio_state = gpio_get(glitcher.serial_pin);
          if (current_gpio_state != last_gpio_state) {
              toggle_count++;
              last_gpio_state = current_gpio_state;
          }

          uint32_t now = time_us_32();
          if (now - last_print > 1000000) { // Every 1 second
              bool pin_state = gpio_get(glitcher.serial_pin);
              if (toggle_count > 0) {
                  printf("\n[SIGNAL DETECTED] Pin GP%d toggled %d times in 1s. Current: %d\n", 
                         glitcher.serial_pin, toggle_count, pin_state);
                  toggle_count = 0;
              } else {
                  printf("%d", pin_state);
              }
              fflush(stdout);
              last_print = now;
          }

          // Manual Trigger Override via button (PIN_BTN_PULSE)
          // `main.c` checks `if (gpio_get(PIN_BTN_PULSE))` for active high button
          if (gpio_get(PIN_BTN_PULSE)) {
              printf("\nManual Trigger!\n");
              
              // Only push the trigger unblock (Addr 9), others already pushed
              pio_sm_put_blocking(pio0, 0, 0); 
              break;
          }
          
          if (uart_is_readable(uart_inst)) {
              // Check for errors first
              uint32_t errors = uart_get_hw(uart_inst)->rsr & 0xF;
              if (errors) {
                  printf("\n[UART ERROR] 0x%X (Framing:%d Overrun:%d Parity:%d Break:%d)\n", 
                         errors, errors&1, (errors>>1)&1, (errors>>2)&1, (errors>>3)&1);
                  uart_get_hw(uart_inst)->rsr = 0xF; // Clear errors
              }

              char c = uart_getc(uart_inst);
              
              if (c >= 32 && c <= 126) {
                  printf("[%c]", c);
              } else {
                  printf("[0x%02X]", (uint8_t)c);
              }
              fflush(stdout);

              if (c == glitcher.serial_pattern[match_idx]) {
                  match_idx++;
                  printf(" (Match: %d/%d)\n", match_idx, pattern_len);
                  fflush(stdout);
                  
                  if (match_idx >= pattern_len) {
                      // Pattern matched, trigger glitch
                      printf("\nPattern matched! Triggering...\n");

                      // Only push the trigger unblock (Addr 9), others already pushed
                      pio_sm_put_blocking(pio0, 0, 0); 
                      
                      break;
                  }
              } else {
                   // Partially reset match: if current char matches start of pattern, start over at 1
                   if (match_idx > 0) {
                       printf(" (Reset match)\n");
                       fflush(stdout);
                   }
                   match_idx = (c == glitcher.serial_pattern[0]) ? 1 : 0;
              }
          }
      }
      
      // Cleanup UART
      uart_deinit(uart_inst);
      gpio_set_function(glitcher.serial_pin, GPIO_FUNC_NULL);
      
      // Since we bypassed the natural PIO IRQ by using Manual push, wait for the PIO program to process it
      while (!pio_interrupt_get(pio0, PIO_IRQ_TRIGGERED)) { }
      
  } else {
      // Standard Trigger IRQ wait
      while (!pio_interrupt_get(pio0, PIO_IRQ_TRIGGERED)) {
         picoemp_process_charging();
         tud_task();
         if ((time_us_32() - start_time) > 3000000) { // 3 seconds timeout
             trigger_timeout = true;
             break;
         }
      }
  }

  if (trigger_timeout) {
      printf("Trigger wait timed out\n");
      adc_run(false);
      dma_channel_abort(ADC_DMA_CHANNEL);
      pio_sm_set_enabled(pio0, 0, false);
      if (current_program.loaded) ft_pio_remove_program(&current_program);
      gpio_put(PIN_LED1, 0);
      return false;
  }

  printf("Trigger successful\n");

  pio_interrupt_clear(pio0, PIO_IRQ_TRIGGERED);
  // gpio_put(PIN_LED2, 1);
  
  // Wait for glitch completion
  if (!pio_interrupt_get_timeout_us(pio0, PIO_IRQ_GLITCHED, 500000)) { // 500ms timeout for glitch
      printf("Glitch wait timed out\n");
  }

  // Stop ADC *immediately* after glitch
  adc_run(false);
  // Abort the infinite DMA ring buffer
  dma_channel_abort(ADC_DMA_CHANNEL);
  adc_fifo_drain();


  pio_interrupt_clear(pio0, PIO_IRQ_GLITCHED);
  pio_sm_set_enabled(pio0, 0, false);
  
  // Final Cleanup
  if (current_program.loaded) {
      ft_pio_remove_program(&current_program);
  }
  
  gpio_put(PIN_LED1, 0);
  
  return true;
}

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
#include "pico/time.h"

#include "board_config.h"

// Original code had PIN_LED1 10, PIN_LED2 9. 
// Let's defer LED changes to avoid behavior change, but unify trigger/glitch pins.

#define PIN_LED1 10
#define PIN_LED2 9

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

#include "uart_rx.pio.h"

#define GLITCHER_SERIAL_BAUD 115200

// Global static to track the loaded PIO program
static struct ft_pio_program current_program = {0};
// Global static to track loaded UART program offset
static uint uart_rx_program_offset = 0;
static bool uart_rx_program_loaded = false;

bool glitcher_configure() {
  struct ft_pio_program *program = &current_program;

  if (current_program.loaded) ft_pio_remove_program(&current_program);
  if (uart_rx_program_loaded) {
      pio_remove_program(pio0, &uart_rx_program, uart_rx_program_offset);
      uart_rx_program_loaded = false;
  }
  
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
      // For serial, SM0 waits for a manual trigger (CPU writes to TX FIFO)
      // This is effectively "pull block"
      {
         uint16_t instr = pio_encode_pull(false, true); 
         ft_pio_program_add_inst(program, instr);
         
         // Also load and configure UART RX on SM 1
         if (!uart_rx_program_loaded) {
             uart_rx_program_offset = pio_add_program(pio0, &uart_rx_program);
             uart_rx_program_loaded = true;
         }
         uart_rx_program_init(pio0, 1, uart_rx_program_offset, glitcher.serial_pin, GLITCHER_SERIAL_BAUD);
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

    sm_config_set_in_pins(&c, trigger_pin);
    pio_gpio_init(pio0, trigger_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, trigger_pin, 1, false);
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
  gpio_put(PIN_LED2, 1);

  prepare_adc();

  // Load FIFO pre-emptively ONLY if not using serial trigger.
  // Secret is: Serial trigger uses an artificial PULL BLOCK as trigger, 
  // so if we preload the FIFO now, it will think the "delay_before_pulse" is the Go-signal!
  if (glitcher.trigger_type != TriggersType_TRIGGER_SERIAL) {
      if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
         pio_sm_put_blocking(pio0, 0, glitcher.power_cycle_length);
      }
      pio_sm_put_blocking(pio0, 0, glitcher.delay_before_pulse);
      if (glitcher.glitch_output != GlitchOutput_None) {
          printf("Glitching...\n");
          pio_sm_put_blocking(pio0, 0, glitcher.pulse_width);
      }
  }
  
  // Serial Trigger Pattern Matching
  if (glitcher.trigger_type == TriggersType_TRIGGER_SERIAL) {
      // Ensure null termination of serial pattern
      glitcher.serial_pattern[sizeof(glitcher.serial_pattern) - 1] = '\0';
      printf("Waiting for serial pattern \"%s\" on GP%d...\n", glitcher.serial_pattern, glitcher.serial_pin);
      fflush(stdout);
      
      uint32_t pattern_len = strlen(glitcher.serial_pattern);
      uint32_t match_idx = 0;
      bool timeout = false;
      uint32_t last_print = 0;
      
      while (!timeout) {
          tud_task(); // Maintain USB connection
          
          uint32_t now = time_us_32();
          
          // Visual heartbeat
          if (now - last_print > 1000000) {
              printf(".");
              fflush(stdout);
              last_print = now;
          }
          
          // Manual Trigger Override
          if (!gpio_get(GLITCHER_TRIGGER_PIN)) {
              printf("\nManual Trigger!\n");
              pio_sm_put_blocking(pio0, 0, 0); // Fake trigger signal
              if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
                 pio_sm_put_blocking(pio0, 0, glitcher.power_cycle_length);
              }
              pio_sm_put_blocking(pio0, 0, glitcher.delay_before_pulse); // Wait
              if (glitcher.glitch_output != GlitchOutput_None) pio_sm_put_blocking(pio0, 0, glitcher.pulse_width); // Fault
              break;
          }

          // Check RX FIFO
          if (!pio_sm_is_rx_fifo_empty(pio0, 1)) {
              char c = uart_rx_program_getc(pio0, 1);
              
              if (c == glitcher.serial_pattern[match_idx]) {
                  match_idx++;
                  if (match_idx >= pattern_len) {
                      // Pattern matched, trigger glitch
                      pio_sm_put_blocking(pio0, 0, 0); // Fake trigger signal
                      if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
                         pio_sm_put_blocking(pio0, 0, glitcher.power_cycle_length);
                      }
                      pio_sm_put_blocking(pio0, 0, glitcher.delay_before_pulse); // Wait
                      if (glitcher.glitch_output != GlitchOutput_None) pio_sm_put_blocking(pio0, 0, glitcher.pulse_width); // Fault
                      break;
                  }
              } else {
                   // Reset match
                   match_idx = 0;
                   if (c == glitcher.serial_pattern[0]) match_idx = 1;
              }
          }
      }
      
      if (timeout) {
         printf("\nSerial trigger timeout\n");
          // Cleanup on timeout
          pio_sm_set_enabled(pio0, 0, false);
          pio_sm_set_enabled(pio0, 1, false);
          dma_channel_abort(ADC_DMA_CHANNEL);
          
          if (current_program.loaded) ft_pio_remove_program(&current_program);
          if (uart_rx_program_loaded) {
              pio_remove_program(pio0, &uart_rx_program, uart_rx_program_offset);
              uart_rx_program_loaded = false;
          }
          gpio_put(PIN_LED1, 0);
          gpio_put(PIN_LED2, 0);
          return false;
      }
  }

  // Wait for Trigger indefinitely
  // Start continuous ADC *before* trigger
  adc_run(true);

  while (!pio_interrupt_get(pio0, PIO_IRQ_TRIGGERED)) {
     tud_task();
  }
  
  if (glitcher.trigger_type == TriggersType_TRIGGER_SERIAL) {
      pio_sm_set_enabled(pio0, 1, false);
  }

  printf("Trigger successful\n");

  pio_interrupt_clear(pio0, PIO_IRQ_TRIGGERED);
  gpio_put(PIN_LED2, 1);
  
  // Wait for glitch completion
  if (!pio_interrupt_get_timeout_us(pio0, PIO_IRQ_GLITCHED, 3000000)) {
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
  if (uart_rx_program_loaded) {
      pio_remove_program(pio0, &uart_rx_program, uart_rx_program_offset);
      uart_rx_program_loaded = false;
  }
  
  gpio_put(PIN_LED1, 0);
  gpio_put(PIN_LED2, 0);
  
  return true;
}

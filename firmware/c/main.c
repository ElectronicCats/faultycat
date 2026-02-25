#include <stdio.h>
#include <string.h>

#include "glitcher.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "picoemp.h"
#include "serial.h"
#include "trigger_basic.pio.h"
#include "board_config.h"

/**
 * @brief Flag to indicate whether we are testing hardware or not.
 *
 * Comment this out to disable hardware testing.
 */
// #define TEST_HARDWARE 1

#ifdef TEST_HARDWARE
#include "hardware/adc.h"
#endif

static bool armed = false;
static bool timeout_active = true;
static bool hvp_internal = true; 
static absolute_time_t timeout_time;
static uint offset = 0xFFFFFFFF;

// defaults taken from original code
#define PULSE_DELAY_CYCLES_DEFAULT 0
#define PULSE_TIME_CYCLES_DEFAULT 625  // 5us in 8ns cycles
#define PULSE_TIME_US_DEFAULT 5        // 5us
#define PULSE_POWER_DEFAULT 0.0122
static uint32_t pulse_time;
static uint32_t pulse_delay_cycles;
static uint32_t pulse_time_cycles;
static union float_union {
  float f;
  uint32_t ui32;
} pulse_power;

void arm() {
  gpio_put(PIN_LED_CHARGE_ON, true);
  armed = true;
}

void disarm() {
  gpio_put(PIN_LED_CHARGE_ON, false);
  gpio_put(PIN_LED_HV, false);
  armed = false;
  picoemp_disable_pwm();
}

uint32_t get_status() {
  uint32_t result = 0;
  if (armed) {
    result |= 0b1;
  }
  if (gpio_get(PIN_IN_CHARGED)) {
    result |= 0b10;
  }
  if (timeout_active) {
    result |= 0b100;
  }
  if (hvp_internal) {
    result |= 0b1000;
  }
  return result;
}

void update_timeout() {
  timeout_time = delayed_by_ms(get_absolute_time(), 60 * 1000);
}

void fast_trigger() {
  // Choose which PIO instance to use (there are two instances)
  PIO pio = pio0;

  // Our assembled program needs to be loaded into this PIO's instruction
  // memory. This SDK function will find a location (offset) in the
  // instruction memory where there is enough space for our program. We need
  // to remember this location!
  if (offset == 0xFFFFFFFF) {  // Only load the program once
    offset = pio_add_program(pio, &trigger_basic_program);
  }

  // Find a free state machine on our chosen PIO (erroring if there are
  // none). Configure it to run our program, and start it, using the
  // helper function we included in our .pio file.
  uint sm = 0;
  trigger_basic_init(pio, sm, offset, PIN_IN_TRIGGER, PIN_OUT_HVPULSE);
  pio_sm_put_blocking(pio, sm, pulse_delay_cycles);
  pio_sm_put_blocking(pio, sm, pulse_time_cycles);
}

#ifdef TEST_HARDWARE
void test_hardware() {
  // For testing purposes, blink GPIOs 0-7 infinitely
  uint8_t trigger_pin = PIN_TRIGGER;

  // Variable to switch the glitch type
  bool switch_glitch_type = false;

  gpio_init(trigger_pin);
  gpio_set_dir(trigger_pin, GPIO_IN);

  // Configure ADC
  adc_init();
  adc_gpio_init(PIN_ADC_INPUT);
  adc_select_input(ADC_CHANNEL_NUM);  // ADC channel corresponding to PIN_ADC_INPUT

  for (uint i = 0; i < 8; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_OUT);
  }

  while (true) {
    for (uint i = 0; i < 8; i++) {
      gpio_put(i, true);
    }
    sleep_ms(500);
    for (uint i = 0; i < 8; i++) {
      gpio_put(i, false);
    }
    sleep_ms(500);

    uint16_t adc_value = adc_read();
    printf("Trigger state: %d, ADC value: %d\n", gpio_get(trigger_pin), adc_value);

    // Switching the glitch type between LP and HP pins
    if(switch_glitch_type){
      glitcher_set_config(TriggersType_TRIGGER_NONE, GlitchOutput_LP, 1000, 2500);
      printf("\nGlitch With LP pulse\n");
    }
    else{
      glitcher_set_config(TriggersType_TRIGGER_NONE, GlitchOutput_HP, 1000, 2500);
      printf("\nGlitch With HP pulse\n");
    }

    // Change value of switching glitch type
    switch_glitch_type = !switch_glitch_type;

    // Run glitch
    glitcher_run();
  }
}
#endif

int main() {
  // Overclock to 250MHz
  set_sys_clock_khz(250000, true);

  // Initialize USB-UART as STDIO
  stdio_init_all();
  // gpio_put(statusLED, true);
  picoemp_init();

  // Init for reset pin (move somewhere else)
  gpio_init(1);
  gpio_set_dir(1, GPIO_OUT);
  gpio_put(1, 1);

  // Run serial-console on second core
  multicore_launch_core1(serial_console);

  pulse_time = PULSE_TIME_US_DEFAULT;
  pulse_power.f = PULSE_POWER_DEFAULT;
  pulse_delay_cycles = PULSE_DELAY_CYCLES_DEFAULT;
  pulse_time_cycles = PULSE_TIME_CYCLES_DEFAULT;

  glitcher_init();

#ifdef TEST_HARDWARE
  test_hardware();
#endif

  while (1) {
    // Handle serial commands (if any)
    while (multicore_fifo_rvalid()) {
      uint32_t command = multicore_fifo_pop_blocking();
      uint32_t val; // Fix undeclared variable
      switch (command) {
        case SERIAL_CMD_arm:
          arm();
          update_timeout();
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_disarm:
          disarm();
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_pulse:
          if (armed) {
            picoemp_pulse(pulse_time);
            update_timeout();
            disarm();
            multicore_fifo_push_blocking(return_ok);
          } else {
            multicore_fifo_push_blocking(return_failed);
          }
          break;
        case SERIAL_CMD_enable_timeout:
          timeout_active = true;
          update_timeout();
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_disable_timeout:
          timeout_active = false;
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_internal_hvp:
          picoemp_configure_pulse_output();
          hvp_internal = true;
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_external_hvp:
          picoemp_configure_pulse_external();
          hvp_internal = false;
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_status:
          multicore_fifo_push_blocking(return_ok);
          multicore_fifo_push_blocking(get_status());
          break;
        case SERIAL_CMD_config_pulse_time:
          pulse_time = multicore_fifo_pop_blocking();
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_config_pulse_power:
          pulse_power.ui32 = multicore_fifo_pop_blocking();
          multicore_fifo_push_blocking(return_ok);
          break;
        case SERIAL_CMD_toggle_gp_all:
          gpio_xor_mask(0xFF);
          multicore_fifo_push_blocking(return_ok);
          break;

        case SERIAL_CMD_fast_trigger:
          // Configure glitcher for fast trigger operation
          // If user hasn't configured, we should probably default to RISING if NONE.
          if (glitcher.trigger_type == TriggersType_TRIGGER_NONE) {
              glitcher.trigger_type = TriggersType_TRIGGER_RISING_EDGE;
          }
          
          // Set Output to EMP (GP14) for fast trigger compatibility
          glitcher.glitch_output = GlitchOutput_EMP; 
          
          multicore_fifo_push_blocking(return_ok);
          bool triggered = glitcher_run();
          
          if (triggered) {
              disarm(); // Turn off charging so it doesn't blink the CHG LED
              multicore_fifo_push_blocking(return_ok);
          } else {
              multicore_fifo_push_blocking(return_failed); // Or any non-ok value to signal timeout
          }
          break;

        case SERIAL_CMD_glitch:
          multicore_fifo_push_blocking(return_ok);
          if (glitcher_run()) {
              disarm(); // Turn off charging
              multicore_fifo_push_blocking(return_ok);
          } else {
              multicore_fifo_push_blocking(return_failed); 
          }
          break;


        case SERIAL_CMD_config_pulse_delay_cycles:
          pulse_delay_cycles = multicore_fifo_pop_blocking();
          glitcher.delay_before_pulse = pulse_delay_cycles;
          multicore_fifo_push_blocking(return_ok);
          break;

        case SERIAL_CMD_config_pulse_time_cycles:
          pulse_time_cycles = multicore_fifo_pop_blocking();
          glitcher.pulse_width = pulse_time_cycles;
          multicore_fifo_push_blocking(return_ok);
          break;
          
        case SERIAL_CMD_config_trigger_type:
          val = multicore_fifo_pop_blocking();
          glitcher.trigger_type = (TriggersType)val;
          multicore_fifo_push_blocking(return_ok);
          break;

        case SERIAL_CMD_config_glitch_output:
          val = multicore_fifo_pop_blocking();
          glitcher.glitch_output = (GlitchOutput_t)val;
          multicore_fifo_push_blocking(return_ok);
          break;

        case SERIAL_CMD_config_trigger_pull:
          val = multicore_fifo_pop_blocking();
          glitcher.trigger_pull_configuration = (TriggerPullConfiguration)val;
          multicore_fifo_push_blocking(return_ok);
          break;
          
        case SERIAL_CMD_config_serial_baud:
          val = multicore_fifo_pop_blocking();
          glitcher.serial_baud = val;
          multicore_fifo_push_blocking(return_ok);
          break;

        case SERIAL_CMD_config_serial_pin:
          val = multicore_fifo_pop_blocking();
          glitcher.serial_pin = val;
          multicore_fifo_push_blocking(return_ok);
          break;

      }
    }

    // Pulse
    if (gpio_get(PIN_BTN_PULSE)) {
      update_timeout();
      picoemp_pulse(pulse_time);
      disarm();
    }

    if (gpio_get(PIN_BTN_ARM)) {
      update_timeout();
      if (!armed) {
        arm();
      } else {
        disarm();
      }
      // YOLO debouncing
      while (gpio_get(PIN_BTN_ARM));
      sleep_ms(100);
    }

    if (armed) {
      gpio_put(PIN_LED_HV, gpio_get(PIN_IN_CHARGED));
      if (!gpio_get(PIN_IN_CHARGED)) {
        picoemp_enable_pwm(pulse_power.f);
      } else {
        picoemp_disable_pwm();
      }
    } else {
      gpio_put(PIN_LED_HV, false);
      picoemp_disable_pwm();
    }

    if (timeout_active && (get_absolute_time() > timeout_time) && armed) {
      disarm();
    }
  }

  return 0;
}

#include <stdio.h>
#include <string.h>

#include "pico/multicore.h"

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/vreg.h"

#include "faultier/faultier/faultier.h"
#include "faultier/faultier/pdnd.h"
#include "faultier/faultier/pdnd_display.h"
#include "tusb.h"

#include "faultier/bootup.h"

#include "faultier/glitcher/delay_compiler.h"
#include "faultier/glitcher/ft_pio.h"
#include "faultier/glitcher/glitch_compiler.h"
#include "faultier/glitcher/power_cycler.h"
#include "faultier/glitcher/trigger_compiler.h"

#include <pb_decode.h>
#include "faultier/proto/faultier.pb.h"
#include "faultier/protocol.h"

#include "faultier/swd/swdchecker.h"
#include "faultier/swd/tamarin_probe.h"

// I think this comes from TinyUSB
void board_init(void);

struct adc_configuration {
  ADCSource source;
  uint32_t sample_count;
};

struct adc_configuration adc_config = {
    .source = ADCSource_ADC_CROWBAR,
    .sample_count = 1000};

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

void display_inputs() {
  cls(false);
  pprintf("Hello World\nInputs:");
  char inputs[32] = "";

  // Iterate all inputs and display them.
  for (int i = 7; i >= 0; i--) {
    if (pdnd_in(i)) {
      strcat(inputs, "1 ");
    } else {
      strcat(inputs, "0 ");
    }
  }
  pprintfxy(0, 40, inputs);
}

struct faultier_adc {
  uint32_t pin;
  uint32_t adc_channel;
};

const struct faultier_adc FT_ADC_CROWBAR = {.pin = ADC_CB_PIN, .adc_channel = ADC_CB};
const struct faultier_adc FT_ADC_MUX = {.pin = ADC_MUX_PIN, .adc_channel = ADC_MUX};
const struct faultier_adc FT_ADC_EXT = {.pin = ADC_EXT_PIN, .adc_channel = ADC_EXT};

#define CAPTURE_DEPTH 30000
uint8_t capture_buffer[CAPTURE_DEPTH];

#define ADC_DMA_CHANNEL 0

static inline void prepare_adc() {
  const struct faultier_adc* adc;
  switch (adc_config.source) {
    case ADCSource_ADC_CROWBAR:
      adc = &FT_ADC_CROWBAR;
      break;
    case ADCSource_ADC_MUX0:
      adc = &FT_ADC_MUX;
      break;
    case ADCSource_ADC_EXT1:
      adc = &FT_ADC_EXT;
      break;
  }

  // overclock B)
  volatile uint32_t* vreg = (volatile uint32_t*)(CLOCKS_BASE + 0x60);
  // Dprintf("adcclk prev %X \n\r",*vreg);
  // Set bit 5 to put adc on sysclk, and set enable too..
  *vreg = 0x820;

  // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
  adc_gpio_init(adc->pin);

  adc_init();
  adc_select_input(adc->adc_channel);
  adc_fifo_setup(
      true,   // Write each completed conversion to the sample FIFO
      true,   // Enable DMA data request (DREQ)
      1,      // DREQ (and IRQ) asserted when at least 1 sample present
      false,  // We won't see the ERR bit because of 8 bit reads; disable.
      true    // Shift each sample to 8 bits when pushing to FIFO
  );

  // Divisor of 0 -> full speed. Free-running capture with the divider is
  // equivalent to pressing the ADC_CS_START_ONCE button once per `div + 1`
  // cycles (div not necessarily an integer). Each conversion takes 96
  // cycles, so in general you want a divider of 0 (hold down the button
  // continuously) or > 95 (take samples less frequently than 96 cycle
  // intervals). This is all timed by the 48 MHz ADC clock.
  adc_set_clkdiv(0);

  // printf("Arming DMA\n");
  // Set up the DMA to start transferring data as soon as it appears in FIFO
  // uint dma_chan = dma_claim_unused_channel(true);
  dma_channel_config cfg = dma_channel_get_default_config(ADC_DMA_CHANNEL);

  // Reading from constant address, writing to incrementing byte addresses
  channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
  channel_config_set_read_increment(&cfg, false);
  channel_config_set_write_increment(&cfg, true);

  // Pace transfers based on availability of ADC samples
  channel_config_set_dreq(&cfg, DREQ_ADC);

  dma_channel_configure(ADC_DMA_CHANNEL, &cfg,
                        capture_buffer,           // dst
                        &adc_hw->fifo,            // src
                        adc_config.sample_count,  // transfer count
                        true                      // start immediately
  );
}

static inline void capture_adc() {
  // printf("Starting capture\n");
  adc_run(true);

  // Once DMA finishes, stop any new conversions from starting, and clean up
  // the FIFO in case the ADC was still mid-conversion.
  dma_channel_wait_for_finish_blocking(ADC_DMA_CHANNEL);
  // printf("Capture finished\n");
  adc_run(false);
  adc_fifo_drain();

  // Print samples to stdout so you can
}

// Signal generator for testing
void core1_entry() {
  gpio_init(PIN_EXT0);
  gpio_set_dir(PIN_EXT0, 1);
  while (1) {
    gpio_put(PIN_EXT0, 1);
    sleep_us(10);
    gpio_put(PIN_EXT0, 0);
    sleep_us(10);
  }
}

void setup_simple_glitcher() {
  struct ft_pio_program program = {0};
  ft_pio_program_init(&program);
  pio_sm_config c = pio_get_default_sm_config();

  if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
    uint power_cycle_pin = 0;
    switch (glitcher.power_cycle_output) {
      case GlitchOutput_OUT_EXT0:
        power_cycle_pin = PIN_EXT0;
        break;
      case GlitchOutput_OUT_EXT1:
        power_cycle_pin = PIN_EXT1;
        break;
      case GlitchOutput_OUT_CROWBAR:
        power_cycle_pin = PIN_GATE;
        break;
      case GlitchOutput_OUT_MUX0:
        power_cycle_pin = PIN_MUX0;
        break;
      case GlitchOutput_OUT_MUX1:
        power_cycle_pin = PIN_MUX1;
        break;
      case GlitchOutput_OUT_MUX2:
        power_cycle_pin = PIN_MUX2;
        break;
    }
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
    int trigger_pin = PIN_EXT1;
    switch (glitcher.trigger_source) {
      case TriggerSource_TRIGGER_IN_EXT0:
        trigger_pin = PIN_EXT0;
        break;
      case TriggerSource_TRIGGER_IN_EXT1:
        trigger_pin = PIN_EXT1;
        break;
      default:
        break;
    }

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
    int glitch_pin = PIN_GATE;
    switch (glitcher.glitch_output) {
      case GlitchOutput_OUT_EXT0:
        glitch_pin = PIN_EXT0;
        break;
      case GlitchOutput_OUT_EXT1:
        glitch_pin = PIN_EXT1;
        break;
      case GlitchOutput_OUT_CROWBAR:
        glitch_pin = PIN_GATE;
        break;
      case GlitchOutput_OUT_MUX0:
        glitch_pin = PIN_MUX0;
        break;
      case GlitchOutput_OUT_MUX1:
        glitch_pin = PIN_MUX1;
        break;
      case GlitchOutput_OUT_MUX2:
        glitch_pin = PIN_MUX2;
        break;
    }

    sm_config_set_set_pins(&c, glitch_pin, 1);
    pio_gpio_init(pio0, glitch_pin);
    pio_sm_set_consecutive_pindirs(pio0, 0, glitch_pin, 1, true);
  }

  pio_sm_init(pio0, 0, program.loaded_offset, &c);
  pio_sm_set_enabled(pio0, 0, true);

  // void ft_pio_run_sm() {
  //     pio_sm_config c = pio_get_default_sm_config();
  //     sm_config_set_set_pins(&c, pin_mosi, 1);
  // }

  // sleep_ms(1000);
  // printf("Removing program\n");
  // ft_pio_remove_program(&program);
}

void writeout(const uint8_t* buf, size_t len) {
  while (true) {
    uint32_t available = tud_cdc_n_write_available(0);
    if (available == 0) {
      tud_task();
      continue;
    }

    tud_task();
    if (len < available) {
      tud_cdc_n_write(0, buf, len);
      len = 0;
      tud_cdc_n_write_flush(0);
    } else {
      tud_cdc_n_write(0, buf, available);
      tud_cdc_n_write_flush(0);
      buf += available;
      len -= available;
    }

    if (len == 0) {
      break;
    }
  }
}

bool readin(uint8_t* buf, size_t len) {
  for (int i = 0; i < len; i++) {
    int32_t c = -1;
    while (c == -1) {
      tud_task();
      c = tud_cdc_n_read_char(0);
    }

    buf[i] = c & 0xFF;
  }

  return true;
}

static inline bool pio_interrupt_get_timeout_us(PIO pio, uint irq, uint timeout) {
  uint32_t absolute_timeout = time_us_32() + timeout;
  while (!pio_interrupt_get(pio, irq)) {
    if (time_us_32() > absolute_timeout) {
      return false;
    }
  }
  return true;
}

static inline char* trigger_to_string(TriggersType trigger) {
  switch (trigger) {
    case TriggerSource_TRIGGER_IN_NONE:
      return "None";
    case TriggerSource_TRIGGER_IN_EXT0:
      return "EXT0";
    case TriggerSource_TRIGGER_IN_EXT1:
      return "EXT1";
  }
  return "Invalid";
}

static inline char* output_to_string(GlitchOutput trigger) {
  switch (trigger) {
    case GlitchOutput_OUT_NONE:
      return "None";
    case GlitchOutput_OUT_CROWBAR:
      return "CB";
    case GlitchOutput_OUT_EXT0:
      return "EXT0";
    case GlitchOutput_OUT_EXT1:
      return "EXT1";
    case GlitchOutput_OUT_MUX0:
      return "MUX0";
    case GlitchOutput_OUT_MUX1:
      return "MUX1";
    case GlitchOutput_OUT_MUX2:
      return "MUX2";
  }
  return "Invalid";
}

uart_parity_t PARITY_TINYUSB_TO_PICO[] = {
    UART_PARITY_NONE,
    UART_PARITY_ODD,
    UART_PARITY_EVEN,
    UART_PARITY_NONE,  // space unsupported
    UART_PARITY_NONE,  // mark unsupported
};

uart_parity_t PARITY_PICO_TO_TINYUSB[] = {
    0,
    2,
    1,
};

uint32_t current_baud = 115200;
uart_parity_t current_parity = UART_PARITY_NONE;

void uart_task(void) {
  // if (tud_cdc_n_connected(1)) {
  //   cdc_line_coding_t coding;
  //   tud_cdc_n_get_line_coding(1, &coding);
  //   if (coding.bit_rate != current_baud) {
  //     current_baud = coding.bit_rate;
  //     uart_init(uart1, current_baud);
  //   }

  //   if (PARITY_TINYUSB_TO_PICO[coding.parity] != current_parity) {
  //     current_parity = PARITY_TINYUSB_TO_PICO[coding.parity];
  //     uart_set_format(uart1, 8, 1, current_parity);
  //   }

  //   if (uart_is_readable(uart1)) {
  //     // cls(false);
  //     // pprintf("UART READABLE!");
  //     char uart_buf;
  //     uart_read_blocking(uart1, &uart_buf, 1);
  //     tud_cdc_n_write_char(1, uart_buf);
  //     tud_cdc_n_write_flush(1);
  //   }
  //   if (tud_cdc_n_available(1)) {
  //     // cls(false);
  //     // pprintf("USB AVAILABLE!");
  //     size_t available = tud_cdc_n_available(1);
  //     while (available) {
  //       char usb_in = tud_cdc_n_read_char(1);
  //       uart_putc_raw(uart1, usb_in);
  //       available--;
  //     }

  //     // uart_write_blocking(uart1, &usb_in, 1);
  //   }
  //   tud_task();
  // }
}

int faultier() {
  // Overclock to 250MHz
  set_sys_clock_khz(250000, true);
  // vreg_set_voltage(VREG_VOLTAGE_1_30);
  // sleep_ms(1);
  // set_sys_clock_khz(444000, true);

  // Initialize Faultier hardware
  faultier_init();

  // Initialize display
  // pdnd_display_initialize();

  // // Display an image on the screen
  // pdnd_display_screen(pdnd_global_display, &bootup);
  // sleep_ms(3000);

  gpio_init(PIN_EXT0);
  gpio_set_dir(PIN_EXT0, 0);
  gpio_pull_down(PIN_EXT0);

  gpio_put(PIN_LED0, 1);
  // multicore_launch_core1(core1_entry);

  // gpio_put(PIN_LED1, 1);
  // gpio_put(PIN_LED2, 1);
  // gpio_put(PIN_MUX1, 1);
  // gpio_put(PIN_MUX2, 1);
  // gpio_put(PIN_MUX3, 1);
  // gpio_put(PIN_GATE, 1);

  // adc_init();

  // adc_gpio_init(ADC_EXT_PIN);
  // adc_select_input(ADC_EXT);

  uart_init(uart1, current_baud);
  gpio_set_function(PIN_IO_BASE + 3, GPIO_FUNC_UART);
  gpio_set_function(PIN_IO_BASE + 4, GPIO_FUNC_UART);

  char serbuf[256];
  uint32_t loop = 0;

  // Initialize USB
  board_init();
  tusb_init();
  tud_init(0);
  tamarin_probe_init();
  while (1) {
    tud_task();
    tamarin_probe_task();
    uart_task();
    if (tud_cdc_n_available(0)) {
      if (!readin(serbuf, 4)) {
        continue;
      }
      if (memcmp(serbuf, "FLTR", 4) != 0) {
        cls(false);
        pprintf("Invalid header.\n%02X%02X%02X%02X", serbuf[0], serbuf[1], serbuf[2], serbuf[3]);
        protocol_error("Invalid header.");
        continue;
      }
      uint32_t length;
      if (!readin((uint8_t*)&length, 4)) {
        cls(false);
        pprintf("Failed to read length.\n");
        continue;
      }

      if (length > 256) {
        cls(false);
        pprintf("Length too large!\n");
        continue;
      }

      if (!readin(serbuf, length)) {
        cls(false);
        pprintf("Failed to read data!\n");
        continue;
      }

      Command command = Command_init_zero;
      pb_istream_t stream = pb_istream_from_buffer(serbuf, length);
      bool status = pb_decode(&stream, Command_fields, &command);
      if (!status) {
        cls(false);
        pprintf("Decoding failed: %s\n", PB_GET_ERROR(&stream));
        continue;
      }

      switch (command.which_cmd) {
        case Command_hello_tag: {
          // Reply with protocol version
          protocol_hello();
        } break;
        case Command_configure_glitcher_tag: {
          CommandConfigureGlitcher gc = command.cmd.configure_glitcher;
          glitcher.configured = true;
          glitcher.power_cycle_output = gc.power_cycle_output;
          glitcher.power_cycle_length = gc.power_cycle_length;
          glitcher.trigger_source = gc.trigger_source;
          glitcher.trigger_type = gc.trigger_type;
          glitcher.glitch_output = gc.glitch_output;
          glitcher.delay = gc.delay;
          glitcher.pulse = gc.pulse;
          glitcher.trigger_pull_configuration = gc.trigger_pull_configuration;

          // ft_cls(false);
          // pprintfxy(0, 9, "D: % 8d", glitcher.delay);
          // pprintfxy(0, 20, "P: % 8d", glitcher.pulse);
          // pprintfxy(0, 31, "T: %s", trigger_to_string(glitcher.trigger_type));
          // pprintfxy(0, 42, "G: %s", output_to_string(glitcher.glitch_output));
          protocol_ok();
        } break;
        case Command_configure_adc_tag: {
          CommandConfigureADC ac = command.cmd.configure_adc;
          if (ac.sample_count > 30000) {
            protocol_error("Maximum sample count is 30000");
            continue;
          }
          adc_config.source = ac.source;
          adc_config.sample_count = ac.sample_count;
          protocol_ok();
        } break;
        case Command_capture_tag:
          break;
        case Command_glitch_tag:
          pio_clear_instruction_memory(pio0);
          setup_simple_glitcher();

          // Ready LED
          gpio_put(PIN_LED1, 1);
          // push power cycle length if enabled
          if (glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
            pio_sm_put_blocking(pio0, 0, glitcher.power_cycle_length);
          }

          // delay
          pio_sm_put_blocking(pio0, 0, glitcher.delay);

          prepare_adc();
          // pulse
          if (glitcher.glitch_output != GlitchOutput_OUT_NONE) {
            pio_sm_put_blocking(pio0, 0, glitcher.pulse);
          }

          // Trigger timeout. Ca. 1 second currently.
          uint trigger_start_millis = to_ms_since_boot(get_absolute_time());
          bool trigger_timed_out = false;
          while (!pio_interrupt_get(pio0, PIO_IRQ_TRIGGERED) && !trigger_timed_out) {
            // Make sure UART bridge keeps working while glitching
            uart_task();
            if ((to_ms_since_boot(get_absolute_time()) - trigger_start_millis) > 1000) {
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

              protocol_trigger_timeout();
              pio_clear_instruction_memory(pio0);
              // TODO: Ensure all pins are back to default
              gpio_put(PIN_LED1, 0);
              gpio_put(PIN_LED2, 0);
              continue;
            }
          }

          capture_adc();

          pio_interrupt_clear(pio0, PIO_IRQ_TRIGGERED);
          // MISC LED
          gpio_put(PIN_LED2, 1);
          // TODO: Fix this area:
          //       - add error reporting for "glitch timeout"
          //       - run uart_task while glitching

          // Hardcoded 3 second timeout for glitcher, no error reporting
          pio_interrupt_get_timeout_us(pio0, PIO_IRQ_GLITCHED, 3000000);
          pio_interrupt_clear(pio0, PIO_IRQ_GLITCHED);
          pio_sm_set_enabled(pio0, 0, false);
          pio_clear_instruction_memory(pio0);
          gpio_put(PIN_LED1, 0);
          gpio_put(PIN_LED2, 0);
          protocol_ok();
          break;
        case Command_read_adc_tag:
          protocol_response_adc(capture_buffer, adc_config.sample_count);
          break;
        case Command_swd_check_tag: {
          uint8_t res;
          switch (command.cmd.swd_check.function) {
            case SWDCheckFunction_SWD_CHECK_ENABLED:
              init_swd();
              tamarin_probe_handle_write("\xa5", 8);
              tamarin_probe_read_mode();
              // turn (yes, slow, for debugging)
              tamarin_probe_read_bits(1);

              res = tamarin_probe_read_bits(3);
              // printf("RES: %d\n", res);
              bool success = 0;
              if (res == 1) {
                success = 1;
              } else {
                success = 0;
              }

              res = tamarin_probe_read_bits(8);
              res = tamarin_probe_read_bits(8);
              res = tamarin_probe_read_bits(8);
              res = tamarin_probe_read_bits(8);
              res = tamarin_probe_read_bits(1);
              tamarin_probe_write_mode();
              tamarin_probe_handle_write("\xFF", 8);

              protocol_swd_check(success);
              break;
            case SWDCheckFunction_SWD_CHECK_NRF52:
              // This code is based on logic-analyzing a working SWD check.
              // It can use a lot of optimization - so feel free :)
              init_swd();
              uint32_t read_data;
              // 0.56143040      Operation       read    DebugPort       IDCODE  0xA5    OK      0x2BA01477      DESIGNER=0x477, PARTNO=0xBA01, Version=0x2
              if (!read_swd(0xA5, &read_data) || (read_data != 0x2BA01477)) {
                // protocol_error("failed to read idcode");
                // continue;
              }
              // 0.56144410      Operation       read    DebugPort       IDCODE  0xA5    OK      0x2BA01477      DESIGNER=0x477, PARTNO=0xBA01, Version=0x2
              // 0.56145798      Operation       read    DebugPort       CTRL/STAT       0x8D    OK      0x00000000      CSYSPWRUPACK=0, CSYSPWRUPREQ=0, CDBGPWRUPACK=0, CDBGPWRUPREQ=0, CDBGRSTACK=0, CDBGRSTREQ=0, TRNCNT=0x000, MASKLANE=0x0, WDATAERR=0, READOK=0, STICKYERR=0, STICKYCMP=0, TRNMODE=Normal, STICKYORUN=0, ORUNDETECT=0
              if (!read_swd(0x8D, &read_data) || (read_data != 0x00000000)) {
                //     protocol_error("INFO: debugport ctrl/stat different\n", read_data);

                //     // continue;
              }
              // 0.56147195      Operation       write   DebugPort       CTRL/STAT       0xA9    OK      0x50000000      CSYSPWRUPACK=0, CSYSPWRUPREQ=1, CDBGPWRUPACK=0, CDBGPWRUPREQ=1, CDBGRSTACK=0, CDBGRSTREQ=0, TRNCNT=0x000, MASKLANE=0x0, WDATAERR=0, READOK=0, STICKYERR=0, STICKYCMP=0, TRNMODE=Normal, STICKYORUN=0, ORUNDETECT=0
              if (!write_swd_int(0xA9, 0x50000000)) {
                protocol_error("Failed to write debugport ctrl/stat1\n");
                continue;
              }
              // 0.56165556      Operation       write   DebugPort       CTRL/STAT       0xA9    OK      0x50000000      CSYSPWRUPACK=0, CSYSPWRUPREQ=1, CDBGPWRUPACK=0, CDBGPWRUPREQ=1, CDBGRSTACK=0, CDBGRSTREQ=0, TRNCNT=0x000, MASKLANE=0x0, WDATAERR=0, READOK=0, STICKYERR=0, STICKYCMP=0, TRNMODE=Normal, STICKYORUN=0, ORUNDETECT=0
              if (!write_swd_int(0xA9, 0x50000000)) {
                protocol_error("Failed to write debugport ctrl/stat2\n");
                continue;
              }
              // 0.56190557      Operation       write   DebugPort       ABORT   0x81    OK      0x0000001E      ORUNERRCLR=1, WDERRCLR=1, STKERRCLR=1, STKCMPCLR=1, DAPABORT=0
              if (!write_swd_int(0x81, 0x0000001E)) {
                protocol_error("Failed to write debugport abort\n");
                continue;
              }
              // 0.56215555      Operation       write   DebugPort       SELECT  0xB1    OK      0x010000F0      APSEL=0x01, APBANKSEL=0xF, PRESCALER=0x0
              if (!write_swd_int(0xB1, 0x010000F0)) {
                protocol_error("Failed to write debugport select\n");
                continue;
              }
              // 0.56240556      Operation       write   AccessPort      RAZ_WI  0xA3    OK      0x23000002
              if (!write_swd_int(0xA3, 0x23000002)) {
                protocol_error("Failed to write accessport razwi\n");
                continue;
              }
              // 0.56265554      Operation       read    AccessPort      IDR     0x9F    WAIT
              if (!read_swd(0x9F, &read_data) || (read_data != 0x00000000)) {
                // printf("INFO: read AP return (this might be wait)\n");
                // continue;
              }
              // 0.56265957      Operation       read    AccessPort      IDR     0x9F    OK      0x00000000      Revision=0x0, JEP-106 continuation=0x0, JEP-106 identity=0x00, Class=This AP is not a Memory Acces Port, AP Identfication=0x00
              if (!read_swd(0x9F, &read_data) || (read_data != 0x00000000)) {
                // printf("INFO: read AP return (this might be wait)\n");
                // continue;
              }
              // 0.56267343      Operation       read    DebugPort       RDBUFF  0xBD    OK      0x02880000
              if (!read_swd(0xBD, &read_data) || (read_data != 0x02880000)) {
                protocol_error("INFO: read DP RDBUF\n");
                continue;
              }
              // 0.56290552      Operation       write   DebugPort       SELECT  0xB1    OK      0x01000000      APSEL=0x01, APBANKSEL=0x0, PRESCALER=0x0
              if (!write_swd_int(0xB1, 0x01000000)) {
                protocol_error("Failed to write DebugPort SELECT\n");
                continue;
              }
              // 0.56315552      Operation       read    AccessPort      DRW     0x9F    OK      0x02880000
              if (!read_swd(0x9F, &read_data) || (read_data != 0x02880000)) {
                protocol_error("INFO: read AccessPort DRW\n");
                continue;
              }
              // 0.56316942      Operation       read    DebugPort       RDBUFF  0xBD    OK      0x00000001
              if (!read_swd(0xBD, &read_data) || (read_data != 0x00000001)) {
                // protocol_error("INFO: read DebugPort RDBUFF2\n");
                protocol_swd_check(0);
                continue;
              }
              protocol_swd_check(1);
              // putchar('A');
              break;
          }
        } break;
        default:
          cls(false);
          pprintf("Invalid command!\n");
          break;
      }
    }
  }

  return 0;
}

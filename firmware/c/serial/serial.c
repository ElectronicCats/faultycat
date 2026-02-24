#include "serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

#include "blueTag.h"
#include "glitcher.h"
#include "glitcher_commands.h"
#include "serial_utils.h"

#define FIRMWARE_VERSION "2.1.0.0"

static char serial_buffer[256];
static char last_command[256];

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

typedef bool (*command_handler_t)(void);

typedef struct {
  const char* name;           // Full command name
  const char* alias;          // Short alias
  const char* description;    // Command description
  command_handler_t handler;  // Function pointer to command handler
  const char* category;       // Category for help grouping
} command_t;

// Command handler prototypes
bool handle_arm();
bool handle_disarm();
bool handle_pulse();
bool handle_enable_timeout();
bool handle_disable_timeout();
bool handle_fast_trigger();
bool handle_fast_trigger_configure();
bool handle_internal_hvp();
bool handle_external_hvp();
bool handle_configure();
bool handle_glitch();
bool handle_configure_glitcher();
bool handle_configure_faultier();
bool handle_glitcher_status();
bool handle_jtag_scan();
bool handle_swd_scan();
bool handle_pin_pulsing();
bool handle_help();
bool handle_toggle_all_gpios();
bool handle_status();
bool handle_reset();
bool handle_configure_adc();
bool handle_display_adc();
bool handle_firmware_version();

// Category
#define CAT_FAULT_INJECTION "Fault Injection"
#define CAT_GLITCH "Glitcher"
#define CAT_PINOUT_SCAN "JTAG / SWD Tools"
#define CAT_SYSTEM "System"

// Command registry
static const command_t commands[] = {
    // Fault Injection Commands
    {"arm", "a", "Arm injection circuit", handle_arm, CAT_FAULT_INJECTION},
    {"disarm", "d", "Disarm injection", handle_disarm, CAT_FAULT_INJECTION},
    {"pulse", "p", "Pulse (manual trigger)", handle_pulse, CAT_FAULT_INJECTION},
    {"enable timeout", "en", "Enable timeout", handle_enable_timeout, CAT_FAULT_INJECTION},
    {"disable timeout", "dt", "Disable timeout", handle_disable_timeout, CAT_FAULT_INJECTION},
    {"fast trigger", "fq", "Fast trigger (quick pulse)", handle_fast_trigger, CAT_FAULT_INJECTION},
    {"fast trigger configure", "fc", "Configure fast trigger", handle_fast_trigger_configure, CAT_FAULT_INJECTION},
    {"internal hvp", "in", "Use internal HV source", handle_internal_hvp, CAT_FAULT_INJECTION},
    {"external hvp", "ex", "Use external HV source", handle_external_hvp, CAT_FAULT_INJECTION},
    {"configure", "cfg", "Configure FI parameters", handle_configure, CAT_FAULT_INJECTION},

    // Glitch Commands
    {"glitch", "g", "Trigger glitch", handle_glitch, CAT_GLITCH},
    {"configure glitcher", "gc", "Configure glitcher", handle_configure_glitcher, CAT_GLITCH},
    {"configure faultier", "cf", "Configure advanced faultier features", handle_configure_faultier, CAT_GLITCH},
    {"glitcher status", "gs", "Show glitcher status", handle_glitcher_status, CAT_GLITCH},
    {"configure adc", "ac", "ADC: configure sampling", handle_configure_adc, CAT_GLITCH},
    {"display adc", "av", "ADC: view sampled data", handle_display_adc, CAT_GLITCH},

    // Pinout Scan Commands
    {"jtag scan", "j", "Scan JTAG chain", handle_jtag_scan, CAT_PINOUT_SCAN},
    {"swd scan", "sw", "Scan SWD targets", handle_swd_scan, CAT_PINOUT_SCAN},
    {"pin pulsing", "pp", "Pulse test pins", handle_pin_pulsing, CAT_PINOUT_SCAN},

    // System Commands
    {"help", "h", "Help (this menu)", handle_help, CAT_SYSTEM},
    {"toggle gpios", "t", "Toggle channels 0-7 for testing", handle_toggle_all_gpios, CAT_SYSTEM},
    {"status", "s", "Show system status", handle_status, CAT_SYSTEM},
    {"reset", "r", "Reset device", handle_reset, CAT_SYSTEM},
    {"version", "v", "Show firmware version", handle_firmware_version, CAT_SYSTEM},

    // End marker
    {NULL, NULL, NULL, NULL, NULL}};

void read_command() {
  memset(serial_buffer, 0, sizeof(serial_buffer));
  while (1) {
    int c = getchar();
    if (c == EOF) {
      return;
    }

    putchar(c);

    if (c == '\r' || c == '\n') {
      if (strlen(serial_buffer) > 0) {
          return;
      } else {
          // Ignore empty lines if we want, or return empty command.
          // Better logic: if we have chars, return. If empty (just newline), maybe return empty to refresh prompt?
          // Existing code returned on \r.
          return;
      }
    }
    // if (c == '\n') { continue; } // Removed this

    // buffer full, just return.
    if (strlen(serial_buffer) >= 255) {
      return;
    }

    serial_buffer[strlen(serial_buffer)] = (char)c;
  }
}

void print_status(uint32_t status) {
  bool armed = (status >> 0) & 1;
  bool charged = (status >> 1) & 1;
  bool timeout_active = (status >> 2) & 1;
  bool hvp_mode = (status >> 3) & 1;
  printf("Fault Injection Status:\n");
  if (armed) {
    printf("- Armed\n");
  } else {
    printf("- Disarmed\n");
  }
  if (charged) {
    printf("- Charged\n");
  } else {
    printf("- Not charged\n");
  }
  if (timeout_active) {
    printf("- Timeout active\n");
  } else {
    printf("- Timeout disabled\n");
  }
  if (hvp_mode) {
    printf("- HVP internal\n");
  } else {
    printf("- HVP external\n");
  }
}

static bool multicore_fifo_pop_safe(uint32_t *data) {
    if (!multicore_fifo_pop_timeout_us(1000000, data)) { // 1 second timeout
        printf("Error: Multicore response timeout!\n");
        return false;
    }
    return true;
}

bool handle_arm(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_arm);
  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("Device armed!\n");
  } else {
    printf("Arming failed!\n");
  }
  return true;
}

bool handle_disarm(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_disarm);
  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("Device disarmed!\n");
  } else {
    printf("Disarming failed!\n");
  }
  return true;
}

bool handle_pulse(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_pulse);
  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("Pulsed!\n");
  } else {
    printf("Pulse failed!\n");
  }
  return true;
}

bool handle_enable_timeout(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_enable_timeout);
  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("Timeout enabled!\n");
  } else {
    printf("Enabling timeout failed!\n");
  }
  return true;
}

bool handle_disable_timeout(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_disable_timeout);
  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("Timeout disabled!\n");
  } else {
    printf("Disabling timeout failed!\n");
  }
  return true;
}

bool handle_fast_trigger(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_fast_trigger);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Fast trigger active...\n");
    uint32_t trigger_result = multicore_fifo_pop_blocking();
    if (trigger_result == return_ok) {
        printf("Triggered!\n");
    } else {
        printf("Trigger timed out!\n");
    }
  } else {
    printf("Setting up fast trigger failed.");
  }
  return true;
}

bool handle_fast_trigger_configure(void) {
  printf("\n=== Fast Trigger Configuration ===\n");
  printf("Configures the glitcher to fire immediately upon detecting a specific edge.\n");
  
  // 1. Trigger Edge
  printf("\n[1/3] Trigger Edge\n");
  printf("Select the signal transition on the Trigger Input pin that will fire the glitch.\n");
  printf("  0: Rising Edge (Low -> High)\n");
  printf("  1: Falling Edge (High -> Low)\n");
  printf("  2: Serial Pattern on GPO0\n");
  
  const char* current_type_str = "Rising";
  if (glitcher.trigger_type == TriggersType_TRIGGER_FALLING_EDGE) current_type_str = "Falling";
  else if (glitcher.trigger_type == TriggersType_TRIGGER_SERIAL) current_type_str = "Serial";
  
  printf("  Current selection: %s\n", current_type_str);
  printf("  > ");
  
  uint32_t edge_val = 0;
  if (glitcher.trigger_type == TriggersType_TRIGGER_FALLING_EDGE) edge_val = 1;
  else if (glitcher.trigger_type == TriggersType_TRIGGER_SERIAL) edge_val = 2;

  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
     if (safe_strtoul(serial_buffer, &edge_val) && edge_val <= 2) {
         // Valid
     } else {
         edge_val = 0; // Default Rising
         printf("Invalid value. Defaulting to Rising Edge.\n");
     }
  } else {
     printf("Keeping default/current (%s).\n", current_type_str);
  }
  
  uint32_t trigger_type;
  if (edge_val == 0) trigger_type = 3; // 3=Rising
  else if (edge_val == 1) trigger_type = 4; // 4=Falling
  else trigger_type = 7; // 7=Serial
  
  const char* new_type_str = "Rising";
  if (edge_val == 1) new_type_str = "Falling";
  else if (edge_val == 2) new_type_str = "Serial";
  
  printf("Selected: %s\n", new_type_str);

  if (trigger_type == 7) { // Serial
      char temp_pattern[32] = {0};
      printf("     Enter serial pattern to wait for on GPO0 (Rx) (Current: \"%s\"): ", glitcher.serial_pattern);
      int i = 0;
      while (i < 31) {
          int c = getchar();
          if (c == '\r' || c == '\n') {
              break;
          } else if (c == '\b' || c == 127) {
              if (i > 0) { i--; printf("\b \b"); }
          } else {
              temp_pattern[i++] = (char)c;
              putchar(c);
          }
      }
      temp_pattern[i] = '\0';
      if (i > 0) {
          printf("\n     Pattern set to: \"%s\"\n", temp_pattern);
          glitcher.serial_pin = 0; // Hardcoded GPO0 (PIN_GATE)
          strncpy(glitcher.serial_pattern, temp_pattern, sizeof(glitcher.serial_pattern) - 1);
      } else {
          printf("\n     Keeping current pattern.\n");
      }
  }

  // 2. Pulse Delay
  printf("\n[2/3] Pulse Delay (Cycles)\n");
  printf("How many clock cycles to wait after the trigger before firing.\n");
  printf("  - 1 cycle = ~8ns (at 125MHz)\n");
  printf("  - 0 = Immediate fire (minimum latency)\n");
  printf("  Current value: %d cycles\n", pulse_delay_cycles);
  printf("  Enter new value (or press Enter to keep current): ");
  
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val)) {
        pulse_delay_cycles = val;
        printf("New Delay: %d cycles\n", pulse_delay_cycles);
    } else {
        printf("Invalid input. Keeping current: %d cycles\n", pulse_delay_cycles);
    }
  } else {
    printf("Keeping current: %d cycles\n", pulse_delay_cycles);
  }

  // 3. Pulse Width
  printf("\n[3/3] Pulse Width (Cycles)\n");
  printf("Duration of the glitch pulse in clock cycles.\n");
  printf("  - Default: %d cycles (~5us)\n", PULSE_TIME_CYCLES_DEFAULT);
  printf("  Current value: %d cycles\n", pulse_time_cycles);
  printf("  Enter new value (or press Enter to keep current): ");
  
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val) && val > 0) {
        pulse_time_cycles = val;
        printf("New Width: %d cycles\n", pulse_time_cycles);
    } else {
        printf("Invalid input (must be > 0). Keeping current: %d cycles\n", pulse_time_cycles);
    }
  } else {
    printf("Keeping current: %d cycles\n", pulse_time_cycles);
  }

  // Send configuration to Core 0 (Main)
  multicore_fifo_push_blocking(SERIAL_CMD_config_pulse_delay_cycles);
  multicore_fifo_push_blocking(pulse_delay_cycles);
  if (!multicore_fifo_pop_safe(NULL)) printf("Error: Timeout syncing pulse_delay_cycles.\n");

  multicore_fifo_push_blocking(SERIAL_CMD_config_pulse_time_cycles);
  multicore_fifo_push_blocking(pulse_time_cycles);
  if (!multicore_fifo_pop_safe(NULL)) printf("Error: Timeout syncing pulse_time_cycles.\n");
  
  multicore_fifo_push_blocking(SERIAL_CMD_config_trigger_type);
  multicore_fifo_push_blocking(trigger_type);
  if (!multicore_fifo_pop_safe(NULL)) printf("Error: Timeout syncing trigger_type.\n");

  printf("\n=== Configuration Complete ===\n");
  printf("Summary:\n");
  printf("  - Trigger: %s \n", new_type_str);
  printf("  - Delay:   %d cycles\n", pulse_delay_cycles);
  printf("  - Width:   %d cycles\n", pulse_time_cycles);

  printf("\n[AUTO] Arming Device and Waiting for Trigger...\n");
  handle_arm();
  handle_fast_trigger();
  printf("\n[AUTO] Disarming Device...\n");
  handle_disarm();

  return true;
}

bool handle_internal_hvp(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_internal_hvp);
  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("Internal HVP mode active!\n");
  } else {
    printf("Setting up internal HVP mode failed.");
  }
  return true;
}

bool handle_external_hvp(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_external_hvp);
  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("External HVP mode active!\n");
  } else {
    printf("Setting up external HVP mode failed.");
  }
  return true;
}

bool handle_configure(void) {
  char** unused;
  printf(" pulse_time (current: %d, default: %d)?\n> ", pulse_time, PULSE_TIME_US_DEFAULT);
  read_command();
  printf("\n");
  if (serial_buffer[0] == 0) {
    printf("Using default\n");
  } else {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val)) {
        pulse_time = val;
    } else {
        printf("Invalid value, keeping current.\n");
    }
  }

  printf(" pulse_power (current: %f, default: %f)?\n> ", pulse_power.f, PULSE_POWER_DEFAULT);
  read_command();
  printf("\n");
  if (serial_buffer[0] == 0)
    printf("Using default");
  else
    pulse_power.f = strtof(serial_buffer, unused);

  multicore_fifo_push_blocking(SERIAL_CMD_config_pulse_time);
  multicore_fifo_push_blocking(pulse_time);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result != return_ok) {
    printf("Config pulse_time failed.");
  }

  multicore_fifo_push_blocking(SERIAL_CMD_config_pulse_power);
  multicore_fifo_push_blocking(pulse_power.ui32);
  result = multicore_fifo_pop_blocking();
  if (result != return_ok) {
    printf("Config pulse_power failed.");
  }

  printf("pulse_time=%d, pulse_power=%f\n", pulse_time, pulse_power.f);

  return true;
}

bool handle_glitch(void) {
  glitcher_run();
  printf("\n[AUTO] Disarming Device...\n");
  handle_disarm();
  return true;
}

const char* get_trigger_type_str(TriggersType type) {
  switch (type) {
    case TriggersType_TRIGGER_NONE: return "None";
    case TriggersType_TRIGGER_HIGH: return "High";
    case TriggersType_TRIGGER_LOW: return "Low";
    case TriggersType_TRIGGER_RISING_EDGE: return "Rising Edge";
    case TriggersType_TRIGGER_FALLING_EDGE: return "Falling Edge";
    case TriggersType_TRIGGER_PULSE_POSITIVE: return "Pulse Positive";
    case TriggersType_TRIGGER_PULSE_NEGATIVE: return "Pulse Negative";
    case TriggersType_TRIGGER_SERIAL: return "Serial";
    default: return "Unknown";
  }
}

const char* get_trigger_pull_str(TriggerPullConfiguration pull) {
  switch (pull) {
    case TriggerPullConfiguration_TRIGGER_PULL_NONE: return "None";
    case TriggerPullConfiguration_TRIGGER_PULL_UP: return "Pull Up";
    case TriggerPullConfiguration_TRIGGER_PULL_DOWN: return "Pull Down";
    default: return "Unknown";
  }
}

const char* get_glitch_output_str(GlitchOutput_t out) {
  switch (out) {
    case GlitchOutput_LP: return "LP";
    case GlitchOutput_HP: return "HP";
    case GlitchOutput_EMP: return "EMP (Antenna)";
    case GlitchOutput_None: return "None";
    default: return "Unknown";
  }
}

bool handle_configure_glitcher(void) {
  printf("\n=== Glitcher Configuration ===\n");
  
  // 1. Trigger Type
  printf("\n[1/5] Trigger Type\n");
  printf("  0: None\n  1: High\n  2: Low\n  3: Rising Edge\n  4: Falling Edge\n  5: Pulse Positive\n  6: Pulse Negative\n  7: Serial\n");
  printf("  Current: %s\n  > ", get_trigger_type_str(glitcher.trigger_type));
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val) && val <= 7) {
      glitcher.trigger_type = (TriggersType)val;
    } else {
      printf("  Invalid. Keeping current.\n");
    }
  }

  if (glitcher.trigger_type == TriggersType_TRIGGER_SERIAL) {
      char temp_pattern[32] = {0};
      printf("     Enter serial pattern to wait for on GPO0 (Rx) (Current: \"%s\"): ", glitcher.serial_pattern);
      int i = 0;
      while (i < 31) {
          int c = getchar();
          if (c == '\r' || c == '\n') {
              break;
          } else if (c == '\b' || c == 127) {
              if (i > 0) { i--; printf("\b \b"); }
          } else {
              temp_pattern[i++] = (char)c;
              putchar(c);
          }
      }
      temp_pattern[i] = '\0';
      if (i > 0) {
          printf("\n     Pattern set to: \"%s\"\n", temp_pattern);
          glitcher.serial_pin = 0; // Hardcoded GPO0 (PIN_GATE)
          strncpy(glitcher.serial_pattern, temp_pattern, sizeof(glitcher.serial_pattern) - 1);
      } else {
          printf("\n     Keeping current pattern.\n");
      }
  }

  // 2. Trigger Pull Configuration
  printf("\n[2/5] Trigger Pull Configuration\n");
  printf("  0: None\n  1: Pull Up\n  2: Pull Down\n");
  printf("  Current: %s\n  > ", get_trigger_pull_str(glitcher.trigger_pull_configuration));
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val) && val <= 2) {
      glitcher.trigger_pull_configuration = (TriggerPullConfiguration)val;
    } else {
      printf("  Invalid. Keeping current.\n");
    }
  }

  // 3. Glitch Output
  printf("\n[3/5] Glitch Output\n");
  printf("  0: None\n  1: LP\n  2: HP\n  3: EMP (Antenna)\n");
  printf("  Current: %s\n  > ", get_glitch_output_str(glitcher.glitch_output));
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val) && val <= 3) {
      glitcher.glitch_output = (GlitchOutput_t)val;
    } else {
      printf("  Invalid. Keeping current.\n");
    }
  }

  // 4. Delay before pulse
  printf("\n[4/5] Delay Before Pulse (cycles)\n");
  printf("  Current: %lu cycles\n  > ", glitcher.delay_before_pulse);
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val)) {
      glitcher.delay_before_pulse = val;
    } else {
      printf("  Invalid. Keeping current.\n");
    }
  }

  // 5. Pulse width
  printf("\n[5/5] Pulse Width (cycles)\n");
  printf("  Current: %lu cycles\n  > ", glitcher.pulse_width);
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val) && val > 0) {
      glitcher.pulse_width = val;
    } else {
      printf("  Invalid (must be > 0). Keeping current.\n");
    }
  }

  printf("\n=== Glitcher Configured ===\n");
  printf("     Configuring glitcher internally...\n");
  glitcher_set_config(glitcher.trigger_type, glitcher.glitch_output, glitcher.delay_before_pulse, glitcher.pulse_width);
  printf("     Glitcher configured successfully\n");

  printf("\n[AUTO] Arming Device and Waiting for Trigger...\n");
  handle_arm();
  handle_glitch();

  return true;
}

bool handle_glitcher_status(void) {
  glitcher_commands_get_config();
  return true;
}

const char* get_trigger_source_str(TriggerSource src) {
  switch(src) {
    case TriggerSource_TRIGGER_IN_NONE: return "None";
    case TriggerSource_TRIGGER_IN_EXT0: return "EXT0";
    case TriggerSource_TRIGGER_IN_EXT1: return "EXT1";
    default: return "Unknown";
  }
}

const char* get_power_cycle_out_str(GlitchOutput out) {
  switch(out) {
    case GlitchOutput_OUT_CROWBAR: return "Crowbar";
    case GlitchOutput_OUT_MUX0: return "MUX0";
    case GlitchOutput_OUT_MUX1: return "MUX1";
    case GlitchOutput_OUT_MUX2: return "MUX2";
    case GlitchOutput_OUT_EXT0: return "EXT0";
    case GlitchOutput_OUT_EXT1: return "EXT1";
    case GlitchOutput_OUT_NONE: return "None";
    case GlitchOutput_OUT_EMP: return "EMP (Antenna)";
    default: return "Unknown";
  }
}

bool handle_configure_faultier(void) {
  printf("\n=== Faultier Configuration ===\n");
  
  // 1. Trigger Source
  printf("\n[1/3] Trigger Source\n");
  printf("  0: None\n  1: EXT0\n  2: EXT1\n");
  printf("  Current: %s\n  > ", get_trigger_source_str(glitcher.trigger_source));
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val) && val <= 2) glitcher.trigger_source = (TriggerSource)val;
    else printf("  Invalid. Keeping current.\n");
  }

  // 1.5 Trigger Type (Add Serial Trigger selection for Faultier)
  printf("\n[Optional] Faultier Trigger Type\n");
  printf("  0: Inherit from Glitcher (Default)\n  7: Serial (GPO0)\n");
  printf("  Current Master Type: %d\n  > ", glitcher.trigger_type);
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val)) {
        if (val == 7) {
            glitcher.trigger_type = TriggersType_TRIGGER_SERIAL;
            
            char temp_pattern[32] = {0};
            printf("     Enter serial pattern to wait for on GPO0 (Rx): ");
            int i = 0;
            while (i < 31) {
                int c = getchar();
                if (c == '\r' || c == '\n') {
                    if (i > 0) break;
                } else if (c == '\b' || c == 127) {
                    if (i > 0) { i--; printf("\b \b"); }
                } else {
                    temp_pattern[i++] = (char)c;
                    putchar(c);
                }
            }
            temp_pattern[i] = '\0';
            printf("\n     Pattern set to: \"%s\"\n", temp_pattern);
            glitcher.serial_pin = 0; // Hardcoded GPO0 (PIN_GATE)
            strncpy(glitcher.serial_pattern, temp_pattern, sizeof(glitcher.serial_pattern) - 1);
        }
    }
  }
  
  // 2. Power Cycle Output
  printf("\n[2/3] Power Cycle Output (Target to cut power to)\n");
  printf("  0: Crowbar\n  1: MUX0\n  2: MUX1\n  3: MUX2\n  4: EXT0\n  5: EXT1\n  6: None\n  7: EMP (Antenna)\n");
  printf("  Current: %s\n  > ", get_power_cycle_out_str(glitcher.power_cycle_output));
  read_command();
  printf("\n");
  if (serial_buffer[0] != 0) {
    uint32_t val;
    if (safe_strtoul(serial_buffer, &val) && val <= 7) glitcher.power_cycle_output = (GlitchOutput)val;
    else printf("  Invalid. Keeping current.\n");
  }

  // 3. Power Cycle Length
  if(glitcher.power_cycle_output != GlitchOutput_OUT_NONE) {
    printf("\n[3/3] Power Cycle Length (Time to keep power off via PIO cycles)\n");
    printf("  Current: %lu cycles\n  > ", glitcher.power_cycle_length);
    read_command();
    printf("\n");
    if (serial_buffer[0] != 0) {
      uint32_t val;
      if (safe_strtoul(serial_buffer, &val)) glitcher.power_cycle_length = val;
      else printf("  Invalid. Keeping current.\n");
    }
  } else {
    printf("\n[3/3] Skipping Power Cycle Length (Output is None)\n");
  }
  
  printf("\n=== Faultier Configured ===\n");

  printf("\n[AUTO] Arming Device and Waiting for Trigger...\n");
  handle_arm();
  handle_glitch();

  return true;
}

bool handle_jtag_scan(void) {
  jtagScan();
  return true;
}

bool handle_swd_scan(void) {
  swdScan();
  return true;
}

bool handle_pin_pulsing(void) {
  jPulsePins = !jPulsePins;
  if (jPulsePins) {
    printf("     Pin pulsing activated.\n\n");
  } else {
    printf("     Pin pulsing deactivated.\n\n");
  }
  return true;
}

bool handle_help(void) {
  // Return false to show help menu
  return false;
}

bool handle_toggle_all_gpios(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_toggle_gp_all);

  uint32_t result;
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    printf("All GPIOs (0-7) toggled successfully.\n");
  } else {
    printf("Toggle all GPIOs failed.\n");
  }

  return true;
}

bool handle_status(void) {
  multicore_fifo_push_blocking(SERIAL_CMD_status);
  uint32_t result;
// ...
  if (!multicore_fifo_pop_safe(&result)) return true;
  if (result == return_ok) {
    uint32_t status_val;
    if (multicore_fifo_pop_safe(&status_val)) {
      print_status(status_val);
    }
  } else {
    printf("Getting status failed!\n");
  }

  printf("\n");
  handle_glitcher_status();

  // CAT_PINOUT_SCAN status
  printf("\nJTAG/SWD scan status:\n");
  printf("- Pin pulsing: %s\n", jPulsePins ? "Enabled" : "Disabled");

  return true;
}

bool handle_reset(void) {
  watchdog_enable(1, 1);
  while (1) {
    // Wait for watchdog reset
  }
  return true;  // This will never be reached
}

void display_command_help(const char* command_name) {
  // Find the command
  for (int i = 0; commands[i].name != NULL; i++) {
    if (strcmp(command_name, commands[i].name) == 0 ||
        strcmp(command_name, commands[i].alias) == 0) {
      printf("=== Command: %s ===\n", commands[i].name);
      printf("Alias: %s\n", commands[i].alias);
      printf("Category: %s\n", commands[i].category);
      printf("Description: %s\n", commands[i].description);
      return;
    }
  }

  printf("Command '%s' not found. Type 'help' for a list of commands.\n", command_name);
}

bool handle_command(char* command) {
  // Check for empty command (repeat last command)
  if (command[0] == 0 && last_command[0] != 0) {
    printf("Repeat previous command (%s)\n", last_command);
    return handle_command(last_command);
  } else if (command[0] != 0) {
    strcpy(last_command, command);
  }

  // Special case for help command
  if (strcmp(command, "h") == 0 || strcmp(command, "help") == 0) {
    return false;  // Return false to show help menu
  }

  // Check for --help suffix
  char base_command[256] = {0};
  bool help_requested = false;

  // Extract the base command if --help is present
  char* help_suffix = strstr(command, " --help");
  if (help_suffix != NULL) {
    size_t base_len = help_suffix - command;
    strncpy(base_command, command, base_len);
    base_command[base_len] = '\0';
    help_requested = true;
  }

  if (help_requested) {
    display_command_help(base_command);
    return true;
  }

  // Find and execute the command (existing code)
  for (int i = 0; commands[i].name != NULL; i++) {
    if (strcmp(command, commands[i].name) == 0 ||
        strcmp(command, commands[i].alias) == 0) {
      return commands[i].handler();
    }
  }

  printf("Unknown command '%s'. Type 'help' for a list of commands.\n", command);
  return true;
}

void display_help() {
  printf("=== FaultyCat 2 Command Menu ===\n\n");

  // Track the current category
  const char* current_category = NULL;

  // Loop through all commands
  for (int i = 0; commands[i].name != NULL; i++) {
    // If we're entering a new category, print the category header
    if (current_category == NULL || strcmp(current_category, commands[i].category) != 0) {
      current_category = commands[i].category;
      printf("\n%s:\n", current_category);
    }

    // Print the command
    printf("[%s] - %s\n",
           commands[i].alias,
           commands[i].description);
  }

  printf("\n");
  printf("- <Enter> - Repeat last command\n");
}

// Add these functions at the end of the file before serial_console()

bool handle_configure_adc(void) {
  char** unused;
  uint32_t current_count = adc_get_sample_count();

  printf(" Configure ADC sample count\n");
  printf(" Current count: %lu (max: 30000)\n", current_count);
  printf(" Enter new sample count: ");

  read_command();
  printf("\n");

  if (serial_buffer[0] == 0) {
    printf(" Using current value: %lu\n", current_count);
    return true;
  }

  uint32_t new_count = strtoul(serial_buffer, unused, 10);

  if (new_count > 30000) {
    printf(" Error: Sample count exceeds maximum (30000)\n");
    return true;
  }

  if (glitcher_set_adc_sample_count(new_count)) {
    printf(" ADC sample count set to: %lu\n", new_count);
  } else {
    printf(" Failed to set ADC sample count\n");
  }

  return true;
}

bool handle_display_adc(void) {
  uint32_t sample_count = adc_get_sample_count();
  uint8_t* buffer = adc_get_capture_buffer();

  printf(" Displaying %lu ADC samples:\n\n", sample_count);

  // Print header
  printf(" Index | Value | Bar\n");
  printf("-------|-------|-------------------\n");

  // Determine the number of samples to display (up to 20 for readability)
  uint32_t display_count = sample_count < 20 ? sample_count : 20;

  // Calculate step size if we need to sample the data
  uint32_t step = sample_count / display_count;
  if (step == 0)
    step = 1;

  // Display samples
  for (uint32_t i = 0; i < sample_count; i += step) {
    if (i >= display_count * step)
      break;

    // Print index and value
    printf(" %5lu | %5u | ", i, buffer[i]);

    // Print simple bar chart representation
    int bar_length = buffer[i] / 10;  // Scale to reasonable length
    for (int j = 0; j < bar_length; j++) {
      printf("#");
    }
    printf("\n");
  }

  printf("\n Note: Displaying %lu out of %lu samples\n", display_count, sample_count);
  printf(" To see all data, use a data visualization tool with the raw values\n");

  return true;
}

bool handle_firmware_version(void) {
  printf("Firmware Version: %s\n", FIRMWARE_VERSION);
  return true;
}

void serial_console() {
  multicore_fifo_drain();
  gpio_init(statusLED);
  gpio_set_dir(statusLED, GPIO_OUT);
  gpio_put(statusLED, true);
  sleep_ms(100);
  gpio_put(statusLED, false);

  memset(last_command, 0, sizeof(last_command));

  pulse_time = PULSE_TIME_US_DEFAULT;
  pulse_power.f = PULSE_POWER_DEFAULT;
  pulse_delay_cycles = PULSE_DELAY_CYCLES_DEFAULT;
  pulse_time_cycles = PULSE_TIME_CYCLES_DEFAULT;

  // BlueTag init
  initChannels();

  // Show help on startup
  sleep_ms(1000);
  display_help();

  while (1) {
    // Show prompt
    if (last_command[0] != 0) {
      printf("[%s] > ", last_command);
    } else {
      printf(" > ");
    }

    read_command();
    printf("\n");

    // Handle command (show help if command returns false)
    if (!handle_command(serial_buffer)) {
      display_help();
    }

    printf("\n");
  }
}

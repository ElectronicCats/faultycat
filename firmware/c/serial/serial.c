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

// Category
#define CAT_FAULT_INJECTION "Fault Injection"
#define CAT_GLITCH "Glitch"
#define CAT_PINOUT_SCAN "SWD/JTAG Scan"
#define CAT_SYSTEM "System"

// Command registry
static const command_t commands[] = {
    // Fault Injection Commands
    {"arm", "a", "Arm the device for fault injection", handle_arm, CAT_FAULT_INJECTION},
    {"disarm", "d", "Disarm the device", handle_disarm, CAT_FAULT_INJECTION},
    {"pulse", "p", "Send a fault injection pulse", handle_pulse, CAT_FAULT_INJECTION},
    {"enable timeout", "en", "Enable timing protection", handle_enable_timeout, CAT_FAULT_INJECTION},
    {"disable timeout", "di", "Disable timing protection", handle_disable_timeout, CAT_FAULT_INJECTION},
    {"fast trigger", "f", "Execute fast trigger sequence", handle_fast_trigger, CAT_FAULT_INJECTION},
    {"fast trigger configure", "fa", "Configure delay and time cycles", handle_fast_trigger_configure, CAT_FAULT_INJECTION},
    {"internal hvp", "in", "Use internal high voltage pulse", handle_internal_hvp, CAT_FAULT_INJECTION},
    {"external hvp", "ex", "Use external high voltage pulse", handle_external_hvp, CAT_FAULT_INJECTION},
    {"configure", "c", "Set pulse time and power", handle_configure, CAT_FAULT_INJECTION},

    // Glitch Commands
    {"glitch", "g", "Execute configured glitch", handle_glitch, CAT_GLITCH},
    {"configure glitcher", "co", "Configure glitcher parameters", handle_configure_glitcher, CAT_GLITCH},
    {"glitcher status", "gl", "Show glitcher configuration", handle_glitcher_status, CAT_GLITCH},
    {"configure adc", "con", "Configure ADC sample count", handle_configure_adc, CAT_GLITCH},
    {"display adc", "di", "Display captured ADC data", handle_display_adc, CAT_GLITCH},

    // Pinout Scan Commands
    {"jtag scan", "j", "Scan for JTAG pinout", handle_jtag_scan, CAT_PINOUT_SCAN},
    {"swd scan", "sw", "Scan for SWD pinout", handle_swd_scan, CAT_PINOUT_SCAN},
    {"pin pulsing", "pi", "Enable/disable pin pulsing", handle_pin_pulsing, CAT_PINOUT_SCAN},

    // System Commands
    {"help", "h", "Show the help menu", handle_help, CAT_SYSTEM},
    {"toggle gpios", "t", "Toggle channels and glitch", handle_toggle_all_gpios, CAT_SYSTEM},
    {"status", "s", "Show device status", handle_status, CAT_SYSTEM},
    {"reset", "r", "Reset the device", handle_reset, CAT_SYSTEM},

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

    if (c == '\r') {
      return;
    }
    if (c == '\n') {
      continue;
    }

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
  printf("Status:\n");
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

bool handle_arm(void) {
  multicore_fifo_push_blocking(cmd_arm);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Device armed!\n");
  } else {
    printf("Arming failed!\n");
  }
  return true;
}

bool handle_disarm(void) {
  multicore_fifo_push_blocking(cmd_disarm);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Device disarmed!\n");
  } else {
    printf("Disarming failed!\n");
  }
  return true;
}

bool handle_pulse(void) {
  multicore_fifo_push_blocking(cmd_pulse);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Pulsed!\n");
  } else {
    printf("Pulse failed!\n");
  }
  return true;
}

bool handle_enable_timeout(void) {
  multicore_fifo_push_blocking(cmd_enable_timeout);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Timeout enabled!\n");
  } else {
    printf("Enabling timeout failed!\n");
  }
  return true;
}

bool handle_disable_timeout(void) {
  multicore_fifo_push_blocking(cmd_disable_timeout);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Timeout disabled!\n");
  } else {
    printf("Disabling timeout failed!\n");
  }
  return true;
}

bool handle_fast_trigger(void) {
  multicore_fifo_push_blocking(cmd_fast_trigger);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Fast trigger active...\n");
    multicore_fifo_pop_blocking();
    printf("Triggered!\n");
  } else {
    printf("Setting up fast trigger failed.");
  }
  return true;
}

bool handle_fast_trigger_configure(void) {
  char** unused;
  printf(" configure in cycles\n");
  printf("  1 cycle = 8ns\n");
  printf("  1us = 125 cycles\n");
  printf("  1ms = 125000 cycles\n");
  printf("  max = MAX_UINT32 = 4294967295 cycles = 34359ms\n");

  printf(" pulse_delay_cycles (current: %d, default: %d)?\n> ", pulse_delay_cycles, PULSE_DELAY_CYCLES_DEFAULT);
  read_command();
  printf("\n");
  if (serial_buffer[0] == 0)
    printf("Using default\n");
  else
    pulse_delay_cycles = strtoul(serial_buffer, unused, 10);

  printf(" pulse_time_cycles (current: %d, default: %d)?\n> ", pulse_time_cycles, PULSE_TIME_CYCLES_DEFAULT);
  read_command();
  printf("\n");
  if (serial_buffer[0] == 0)
    printf("Using default\n");
  else
    pulse_time_cycles = strtoul(serial_buffer, unused, 10);

  multicore_fifo_push_blocking(cmd_config_pulse_delay_cycles);
  multicore_fifo_push_blocking(pulse_delay_cycles);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result != return_ok) {
    printf("Config pulse_delay_cycles failed.");
  }

  multicore_fifo_push_blocking(cmd_config_pulse_time_cycles);
  multicore_fifo_push_blocking(pulse_time_cycles);
  result = multicore_fifo_pop_blocking();
  if (result != return_ok) {
    printf("Config pulse_time_cycles failed.");
  }

  printf("pulse_delay_cycles=%d, pulse_time_cycles=%d\n", pulse_delay_cycles, pulse_time_cycles);

  return true;
}

bool handle_internal_hvp(void) {
  multicore_fifo_push_blocking(cmd_internal_hvp);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("Internal HVP mode active!\n");
  } else {
    printf("Setting up internal HVP mode failed.");
  }
  return true;
}

bool handle_external_hvp(void) {
  multicore_fifo_push_blocking(cmd_external_hvp);
  uint32_t result = multicore_fifo_pop_blocking();
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
  if (serial_buffer[0] == 0)
    printf("Using default\n");
  else
    pulse_time = strtoul(serial_buffer, unused, 10);

  printf(" pulse_power (current: %f, default: %f)?\n> ", pulse_power.f, PULSE_POWER_DEFAULT);
  read_command();
  printf("\n");
  if (serial_buffer[0] == 0)
    printf("Using default");
  else
    pulse_power.f = strtof(serial_buffer, unused);

  multicore_fifo_push_blocking(cmd_config_pulse_time);
  multicore_fifo_push_blocking(pulse_time);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result != return_ok) {
    printf("Config pulse_time failed.");
  }

  multicore_fifo_push_blocking(cmd_config_pulse_power);
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
  return true;
}

bool handle_configure_glitcher(void) {
  glitcher_commands_configure();
  return true;
}

bool handle_glitcher_status(void) {
  glitcher_commands_get_config();
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
  multicore_fifo_push_blocking(cmd_toggle_gp_all);

  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    printf("All GPIOs (0-7) toggled successfully.\n");
  } else {
    printf("Toggle all GPIOs failed.\n");
  }

  return true;
}

bool handle_status(void) {
  multicore_fifo_push_blocking(cmd_status);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result == return_ok) {
    print_status(multicore_fifo_pop_blocking());
  } else {
    printf("Getting status failed!\n");
  }
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
  printf("=== FaultyCat Command Interface ===\n\n");

  // Track the current category
  const char* current_category = NULL;

  // Loop through all commands
  for (int i = 0; commands[i].name != NULL; i++) {
    // If we're entering a new category, print the category header
    if (current_category == NULL || strcmp(current_category, commands[i].category) != 0) {
      current_category = commands[i].category;
      printf("\n%s Commands:\n", current_category);
    }

    // Print the command
    printf("- [%s]%s\n",
           commands[i].alias,
           commands[i].name + strlen(commands[i].alias));
  }

  printf("\n");
  printf("- <empty> - Repeat last command\n");
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

void serial_console() {
  multicore_fifo_drain();

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

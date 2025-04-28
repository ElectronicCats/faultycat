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
bool handle_toggle_gp1();
bool handle_status();
bool handle_reset();

// Command registry
static const command_t commands[] = {
    // Fault Injection Commands
    {"arm", "a", "Arm the device for fault injection", handle_arm, "Fault Injection"},
    {"disarm", "d", "Disarm the device", handle_disarm, "Fault Injection"},
    {"pulse", "p", "Send a fault injection pulse", handle_pulse, "Fault Injection"},
    {"enable_timeout", "en", "Enable timing protection", handle_enable_timeout, "Fault Injection"},
    {"disable_timeout", "di", "Disable timing protection", handle_disable_timeout, "Fault Injection"},
    {"fast_trigger", "f", "Execute fast trigger sequence", handle_fast_trigger, "Fault Injection"},
    {"fast_trigger_configure", "fa", "Configure delay and time cycles", handle_fast_trigger_configure, "Fault Injection"},
    {"internal_hvp", "in", "Use internal high voltage pulse", handle_internal_hvp, "Fault Injection"},
    {"external_hvp", "ex", "Use external high voltage pulse", handle_external_hvp, "Fault Injection"},
    {"configure", "c", "Set pulse time and power", handle_configure, "Fault Injection"},

    // Glitch Commands
    {"glitch", "g", "Execute configured glitch", handle_glitch, "Glitch"},
    {"configure_glitcher", "cg", "Configure glitcher parameters", handle_configure_glitcher, "Glitch"},
    {"glitcher_status", "gs", "Show glitcher configuration", handle_glitcher_status, "Glitch"},

    // Pinout Scan Commands
    {"jtag_scan", "j", "Scan for JTAG pinout", handle_jtag_scan, "Pinout Scan"},
    {"swd_scan", "sw", "Scan for SWD pinout", handle_swd_scan, "Pinout Scan"},
    {"pin_pulsing", "pp", "Enable/disable pin pulsing", handle_pin_pulsing, "Pinout Scan"},

    // System Commands
    {"help", "h", "Show this help menu", handle_help, "System"},
    {"toggle_gp1", "t", "Toggle GPIO pin 1", handle_toggle_gp1, "System"},
    {"status", "s", "Show device status", handle_status, "System"},
    {"reset", "r", "Reset the device", handle_reset, "System"},

    // End marker
    {NULL, NULL, NULL, NULL, NULL}};

void read_line() {
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

// Example handler for the arm command
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

// Example handler for the disarm command
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
  read_line();
  printf("\n");
  if (serial_buffer[0] == 0)
    printf("Using default\n");
  else
    pulse_delay_cycles = strtoul(serial_buffer, unused, 10);

  printf(" pulse_time_cycles (current: %d, default: %d)?\n> ", pulse_time_cycles, PULSE_TIME_CYCLES_DEFAULT);
  read_line();
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
  read_line();
  printf("\n");
  if (serial_buffer[0] == 0)
    printf("Using default\n");
  else
    pulse_time = strtoul(serial_buffer, unused, 10);

  printf(" pulse_power (current: %f, default: %f)?\n> ", pulse_power.f, PULSE_POWER_DEFAULT);
  read_line();
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

bool handle_toggle_gp1(void) {
  multicore_fifo_push_blocking(cmd_toggle_gp1);
  uint32_t result = multicore_fifo_pop_blocking();
  if (result != return_ok) {
    printf("target_reset failed.");
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
  return true; // This will never be reached
}

bool handle_command(char* command) {
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

  // Find and execute the command
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
    printf("- [%s]%-20s - %s\n",
           commands[i].alias,
           commands[i].name + strlen(commands[i].alias),
           commands[i].description);
  }

  printf("\n");
  printf("- <empty>                  - Repeat last command\n");
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

    // Read command
    read_line();
    printf("\n");

    // Handle command (show help if command returns false)
    if (!handle_command(serial_buffer)) {
      display_help();
    }

    printf("\n");
  }
}

#pragma once

#include <stdbool.h>

// drivers/ext_trigger — external trigger input on GP8 (v2.1+).
//
// This driver exposes the raw pin state and the pull-resistor
// configuration. Edge/level/pulse semantics (rising, falling, high,
// low, positive pulse, negative pulse, serial pattern) are policy
// and live in services/glitch_engine/* (F4 and F5). Drivers have no
// policy — that rule is enforced here.
//
// The v2.x board puts a level-shifter between the actual trigger
// input and GP8, with a VREF tied to TRIGGER_VREF. So "active high"
// on GP8 means "active at whatever threshold TRIGGER_VREF sets";
// this driver cannot distinguish and doesn't try to.

typedef enum {
    EXT_TRIGGER_PULL_NONE = 0,
    EXT_TRIGGER_PULL_UP   = 1,
    EXT_TRIGGER_PULL_DOWN = 2,
} ext_trigger_pull_t;

// Configure GP8 as input with the requested internal pull.
void ext_trigger_init(ext_trigger_pull_t pull);

// Change only the pull configuration, keeping direction as input.
void ext_trigger_set_pull(ext_trigger_pull_t pull);

// Return the current electrical level of GP8 (true = high).
// Edge detection belongs to the service layer; this is a raw read.
bool ext_trigger_level(void);

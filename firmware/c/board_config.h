#pragma once

// PicoEMP / FaultyCat Pin Configuration

// Trigger Input
// Note: Board has Trigger on GP8.
#define PIN_TRIGGER          8

// High Voltage Control
#define PIN_HV_PWM           20
#define PIN_HV_PULSE         14
#define PIN_HV_FB_IN         18 // Charged signal (Active Low)

// LEDs
#define PIN_LED_HV_ARMED     9
#define PIN_LED_CHARGE_ON    27
#define PIN_LED_STATUS       25 // Onboard LED

// Buttons
#define PIN_BTN_ARM          28
#define PIN_BTN_PULSE        11

// Glitcher Specific
#define PIN_GLITCH_LP        16
#define PIN_GLITCH_HP        17

// ADC
#define PIN_ADC_INPUT        29
#define ADC_CHANNEL_NUM      3

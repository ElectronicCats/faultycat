# Porting analysis — legacy v2.x → v3

## Summary

Firmware v3 is a from-scratch rewrite for the existing FaultyCat
v2.x hardware. The legacy v2.x firmware tree (`firmware/c/`) was
the reference during the rewrite but is **no longer present in
this repo** — it was dropped after F11 cleanup once the v3 stack
proved equivalent on real boards.

This document is kept as a brief historical record. The
authoritative description of the v3 layering and pin map lives in:

- [`docs/ARCHITECTURE.md`](ARCHITECTURE.md) — HAL → drivers →
  services → apps, USB composite layout, host-tool module map.
- [`docs/HARDWARE_V2.md`](HARDWARE_V2.md) — GPIO → function map
  for the v2.1 / v2.2 board.

## What v3 inherits 1:1 from the v2.x firmware (HW-proven)

- `picoemp_enable_pwm(duty)` — the 2.5 kHz flyback PWM with
  `0.0122` duty default. Now in `drivers/hv_charger/`.
- `PIN_LED_HV` hysteresis with 500 ms hold. Now in
  `drivers/ui_leds::ui_leds_hv_detected_feed`.
- Button polarity (ARM active-high pulldown, PULSE active-low
  pullup), normalised in software so the HAL stays portable.
- 60 s auto-disarm as a driver-level safety default in
  `drivers/hv_charger::hv_charger_tick`.
- `picoemp_pulse(width_us)` — CPU-timed pulse with interrupts
  disabled. Now in `drivers/emfi_pulse`. PIO-driven triggered
  fire moved to the service layer in F4.

## What v3 explicitly abandons

- `multicore_fifo` control plane (core1 running the serial
  console). v3 is single-core cooperative with a TinyUSB task loop.
- Nanopb encoding. Custom binary protocols over CDC + standard
  CMSIS-DAP are simpler and decouple us from a codegen dependency.
- `stdio_usb` redirection of printf to the sole CDC. v3's CDCs
  are typed by purpose; diagnostic logging moves to a dedicated
  scanner CDC.

## What v3 corrects while porting

- Dropped `PIN_EXT1 = 27` (collided with the charge-on LED on
  real v2.x). Confirmed physically.
- Dropped `PIN_LED_STATUS = 25` (Pico-module relic; v2.x has no
  LED on GP25). The real STATUS LED is GP10.
- Dropped `PIN_MUX0/1/2 = GP1/GP2/GP3`; those GPIOs are scanner
  CH1/CH2/CH3 on v2.x and no HW mux exists.
- Collapsed `GlitchOutput_None / LP / HP / EMP` into two
  orthogonal axes: which service owns the path (EMFI vs crowbar
  — separate state machines, separate PIO SMs, separate IRQ
  flags) and which physical output the crowbar PIO drives
  (`crowbar_out_t` = LP/HP, picked at fire time). EMFI lives in
  its own service tree, not as an enum value of the same selector.

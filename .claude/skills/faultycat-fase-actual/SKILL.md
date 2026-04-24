---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md`, `docs/SAFETY.md` antes de
> tocar nada. Las 16 decisiones congeladas del plan §1 **no se
> relitigan**.

## Fase actual: F3 cerrada → F4 a continuación (EMFI glitch_engine service)

## Tags cerrados en `rewrite/v3`

### `v3.0-f0` — Bootstrap + vendoring + docs + CI (2026-04-23)
Submódulos pineados: pico-sdk@2.1.1, debugprobe@v2.3.0, blueTag@v2.1.2,
free-dap@master, faultier@1c78f3e (ref only), cmsis-dap headers
Apache-2.0.

### `v3.0-f1` — HAL + host tests (2026-04-23)
Unity@v2.6.1. HAL lifted: `gpio.h`, `time.h`. Stubs: `pio.h`, `dma.h`,
`usb.h`.

### `v3.0-f2a` — drivers low-risk (2026-04-23)
`board_v2.h` + `ui_leds`, `ui_buttons`, `target_monitor`, `scanner_io`,
`ext_trigger`. HAL lifted: `adc.h`.

### `v3.0-f2b` — drivers HV (2026-04-23)
`crowbar_mosfet`, `voltage_mux` (stub), `hv_charger` SIGNED,
`emfi_pulse` SIGNED. HAL lifted: `pwm.h`. `time.h` +busy_wait_us +irq
control. `docs/SAFETY.md` creado.

### `v3.0-f3` — USB composite (2026-04-23)
VID:PID `1209:FA17`. 10 interfaces (4×CDC + Vendor CMSIS-DAP v2 +
HID CMSIS-DAP v1). 16/16 endpoints (RP2040 hard limit). BOS + MS OS
2.0 for Windows WinUSB auto-bind. Magic baud 1200 on any CDC
triggers `reset_usb_boot` (remote BOOTSEL). `tools/bootsel.sh` +
`tools/flash.sh` auto-kick into BOOTSEL. Diag stream migrated from
stdio_usb to CDC2 scanner. `dap_stub.c` shared by v1 HID + v2 vendor,
DAP_Info responds correctly (strings + caps + packet size). F7
replaces dap_stub with the full debugprobe-derived DAP engine.

## Estado del HAL

| Header | Estado | Lifted / planned |
|--------|--------|------------------|
| `hal/gpio.h` | ✓ | F1 |
| `hal/time.h` | ✓ (+busy_wait_us +irq ctl en F2b) | F1 / F2b |
| `hal/adc.h`  | ✓ | F2a |
| `hal/pwm.h`  | ✓ | F2b |
| `hal/pio.h`  | `#error` stub | **F4** (glitch engine) |
| `hal/dma.h`  | `#error` stub | **F4** (ADC capture ring) |
| `hal/usb.h`  | `#error` stub | **NO se levanta** — TinyUSB es la abstracción |

## Estado de drivers

9 drivers: `ui_leds`, `ui_buttons`, `target_monitor`, `scanner_io`,
`ext_trigger`, `crowbar_mosfet`, `hv_charger` (SIGNED),
`emfi_pulse` (SIGNED), `voltage_mux` (`#error` stub).

## Estado USB

Composite activo en **1209:FA17**:
- 4 CDC: emfi(0), crowbar(1), scanner(2), target-uart(3) — todos
  con echo default, scanner tiene diag activo.
- Vendor CMSIS-DAP v2 (IF 8) + HID CMSIS-DAP v1 (IF 9) — ambos
  stub (DAP_Info responde, otras DAP commands → DAP_ERROR). F7
  implementa el engine real.
- Windows auto-bind via MS OS 2.0 (WinUSB, GUID compartida con
  debugprobe).
- Magic baud 1200 → BOOTSEL. `tools/flash.sh` lo usa.

## Tests

75 Unity cases en 10 binarios, verde. `host-tests` preset + CI.

## En qué estamos ahora — F4 (EMFI glitch engine service)

Objetivo: primer servicio real. Orquesta `drivers/hv_charger`,
`drivers/emfi_pulse`, `drivers/ext_trigger`, `drivers/target_monitor`
para ejecutar campañas EMFI con trigger externo y ADC capture.
Ver plan §6 F4.

Entregables esperados:
- `services/glitch_engine/emfi/emfi_campaign.{h,c}` — API:
  `emfi_configure(trigger_type, delay, width, pwr)`, `emfi_arm`,
  `emfi_fire`, `emfi_status`.
- **Levanta `hal/pio.h`** — wraps RP2040 PIO (state machines, FIFO,
  IRQs). Ports of the faultier trigger/delay/glitch compiler
  architecture (REFERENCE ONLY — reescribir desde cero, faultier
  sin licencia).
- **Levanta `hal/dma.h`** — para el ADC capture ring (ring-buffer
  8192 bytes como el legacy glitcher.c).
- `services/host_proto/emfi_proto/` — protocolo binario sobre CDC0
  para configurar + armar + fire desde host.
- Integración en `apps/faultycat_fw/main.c` — exponer emfi_proto
  sobre CDC0 del composite.

Criterio F4:
- `tud_cdc_0_available()` → parser emfi_proto → `emfi_configure`.
- Un comando `arm` desde host arma HV.
- Un comando `fire` con trigger externo en GP8 dispara EMFI PIO-
  timed (no CPU-timed).
- ADC captura 8 KB durante el glitch window.
- Pulso EMFI se observa en osciloscopio con timing sub-µs
  reproducible.

**F4 es safety-gate activa** — cualquier commit que toque
`hal/src/rp2040/pio.c` si soporta el EMFI path, o que cambie
`drivers/emfi_pulse` requiere checklist firmado por Sabas.

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x.
- `Hardware/` — KiCad.
- `third_party/*` — pineados.
- `third_party/faultier/` — **jamás** portar código literal
  (`LICENSES/NOTICE-faultier.md`). F4 reimplementa desde cero la
  arquitectura del trigger compiler.
- En F4: no empezar F5 antes del tag `v3.0-f4` + validación scope
  del pulso PIO + captura ADC.

## Reglas de oro

1. **Faseo estricto** (F(N) arranca con tag validado de F(N-1)).
2. **HV safety gate** (activa desde F2b, incluye F4). Todo commit
   a `drivers/{hv_charger,emfi_pulse,crowbar_mosfet}` o a
   `hal/src/rp2040/{pwm,pio}.c` en su soporte HV requiere checklist
   firmado.
3. **faultier sin licencia** — solo referencia arquitectural.
4. **Tests antes de commit**.
5. **Drivers no conocen política** — servicios deciden.
6. **Docs live-update** — cada commit que cierra una fase actualiza
   `ARCHITECTURE.md` (status snapshot + tree) y `PORTING.md` (status
   cells). Separated doc commit inmediatamente antes del tag de la
   fase es aceptable; el tag va sobre el docs commit para que el
   snapshot coincida con la etiqueta.

## Reglas extra activas ahora mismo (F4)

- **PIO global state**: sola una instancia `pio0` o `pio1` por
  programa; la arquitectura ft_pio (de faultier) compone trigger +
  delay + glitch + power_cycle como un solo programa PIO linealizado.
  Reescribir desde cero.
- **ADC DMA ring**: 8 KB circular buffer + DMA ring-mode (13-bit
  alignment) + DREQ_ADC. Ya hay código legacy en
  `firmware/c/glitcher/glitcher.c::prepare_adc` que sirve de
  referencia (LICENCIA OK — legacy propio, no faultier).
- **Diag durante F4**: reportar capturas ADC via `CDC0 emfi` como
  protocolo binario. CDC2 scanner sigue siendo el diag humano.
- **No romper F3**: composite + vendor IF + HID IF siguen operando
  durante F4 — los host tools existentes (`lsusb`, `picocom`,
  `pyusb DAP_Info`) no deben regresionar.

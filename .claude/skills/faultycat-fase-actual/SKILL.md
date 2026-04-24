---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md`, `docs/SAFETY.md` antes de
> tocar nada. Las 16 decisiones congeladas del plan §1 **no se
> relitigan**.

## Fase actual: F4 cerrada → F5 a continuación (crowbar glitch_engine service)

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

### `v3.0-f4` — EMFI glitch engine + host proto (2026-04-24)
hal/pio + hal/dma lifted. services/glitch_engine/emfi/ complete:
emfi_pio (PIO trigger compiler, reimplemented from scratch),
emfi_capture (8 KB ADC DMA ring on GP29), emfi_campaign (state
machine + 100 ms HV invariant activated). services/host_proto/
emfi_proto on CDC0 with CRC16-CCITT framing. Button PULSE kept on
CPU fire path (F2b) for operator use. tools/emfi_client.py reference
host client. SAFETY.md §3 #5 activated in F4-5.

## Estado del HAL

| Header | Estado | Lifted / planned |
|--------|--------|------------------|
| `hal/gpio.h` | ✓ | F1 |
| `hal/time.h` | ✓ (+busy_wait_us +irq ctl en F2b) | F1 / F2b |
| `hal/adc.h`  | ✓ | F2a |
| `hal/pwm.h`  | ✓ | F2b |
| `hal/pio.h`  | ✓ | F4-1 |
| `hal/dma.h`  | ✓ | F4-2 |
| `hal/usb.h`  | `#error` stub | **NO se levanta** — TinyUSB es la abstracción |

## Estado de drivers

9 drivers: `ui_leds`, `ui_buttons`, `target_monitor`, `scanner_io`,
`ext_trigger`, `crowbar_mosfet`, `hv_charger` (SIGNED),
`emfi_pulse` (SIGNED), `voltage_mux` (`#error` stub).

## Estado USB

Composite activo en **1209:FA17**:
- 4 CDC: emfi(0) → emfi_proto binary, crowbar(1) echo (F5), scanner(2) diag, target-uart(3) echo.
- Vendor CMSIS-DAP v2 (IF 8) + HID CMSIS-DAP v1 (IF 9) — ambos
  stub (DAP_Info responde, otras DAP commands → DAP_ERROR). F7
  implementa el engine real.
- Windows auto-bind via MS OS 2.0 (WinUSB, GUID compartida con
  debugprobe).
- Magic baud 1200 → BOOTSEL. `tools/flash.sh` lo usa.

## Tests

155 Unity cases en 16 binarios, verde. `host-tests` preset + CI.

## En qué estamos ahora — F5 (crowbar glitch_engine service)

Siguiente servicio: voltage glitching con crowbar MOSFET. Plan §6 F5.
Reusa el patrón de emfi_campaign: service compone drivers + PIO.
Safety gate cubre crowbar_mosfet si cambia break-before-make.

Pautas clave ya validadas en F4:
- Service owns PIO program build-from-scratch.
- 100 ms HV invariant pattern (si aplica a HV path; crowbar
  doesn't use HV per se pero sí drives peak current spikes).
- host_proto/* pattern con CRC16-CCITT framing replicable para
  crowbar_proto.

Entregables F5 pendientes — escribir plan detallado antes de tocar
código.

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x.
- `Hardware/` — KiCad.
- `third_party/*` — pineados.
- `third_party/faultier/` — **jamás** portar código literal
  (`LICENSES/NOTICE-faultier.md`).
- En F5: no empezar F6 antes del tag `v3.0-f5`.

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

## Reglas extra activas ahora mismo (F5)

- **PIO**: `pio0` dedicado a los glitch engines. `emfi_pio` ya usa
  SM0. F5 asigna SMs adicionales en pio0 al crowbar path. `pio1`
  queda reservado para SWD (F6), target-uart y scanner (F8).
- **host_proto pattern**: `crowbar_proto` sobre CDC1 replica el
  shape de emfi_proto — CRC16-CCITT, SOF 0xFA, inter-byte timeout
  100 ms, reply `CMD|0x80`.
- **No romper F3/F4**: composite + vendor IF + HID IF siguen
  operando; `pump_emfi_cdc` en main.c debe seguir vivo. Los host
  tools existentes (`lsusb`, `picocom /dev/ttyACM2`,
  `tools/emfi_client.py ping`) no deben regresionar.

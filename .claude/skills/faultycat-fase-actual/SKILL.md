---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md`, `docs/SAFETY.md` antes de
> tocar nada. Las 16 decisiones congeladas del plan §1 **no se
> relitigan**.

## Fase actual: F2b cerrada → F3 a continuación (USB composite descriptor)

## Tags cerrados en `rewrite/v3`

### `v3.0-f0` — Bootstrap + vendoring + docs + CI (2026-04-23)
Submódulos pineados en `third_party/`:
- `pico-sdk@2.1.1` (bddd20f), `debugprobe@debugprobe-v2.3.0` (780b827),
  `blueTag@v2.1.2` (887fc83), `free-dap@master` (49a30aa),
  `faultier@1c78f3e` (ref-only, NO compila),
  `cmsis-dap/` headers Apache-2.0.

### `v3.0-f1` — HAL + host tests (2026-04-23)
- `Unity@v2.6.1` (6º submódulo).
- HAL lifted: `gpio.h`, `time.h`.
- HAL stubs (`#error`): `pio.h`, `dma.h`, `usb.h`.

### `v3.0-f2a` — drivers low-risk (2026-04-23)
- `drivers/include/board_v2.h` (pin map autoritativo).
- Drivers: `ui_leds`, `ui_buttons`, `target_monitor`, `scanner_io`,
  `ext_trigger`.
- HAL lifted: `adc.h`.

### `v3.0-f2b` — drivers HV (2026-04-23)
- Drivers: `crowbar_mosfet`, `voltage_mux` (stub), `hv_charger`
  (SIGNED), `emfi_pulse` (SIGNED).
- HAL lifted: `pwm.h`. `time.h` extendido con `busy_wait_us` +
  `irq_save_and_disable` / `_restore`.
- `docs/SAFETY.md` creado con template de checklist + procedures.
- 2 commits con safety checklist firmado por Sabas en el cuerpo:
  `f450d43` (hv_charger) y `69792ac` (emfi_pulse).
- Físico: HV charger carga + auto-disarm 60 s + EMFI fire en SMA
  con coil montado, pulso ~5 µs visible.

## Estado del HAL

| Header | Estado | Lifted en |
|--------|--------|-----------|
| `hal/gpio.h` | ✓ activo | F1 |
| `hal/time.h` | ✓ activo (+busy_wait_us +irq ctl en F2b) | F1 / F2b |
| `hal/adc.h`  | ✓ activo | F2a |
| `hal/pwm.h`  | ✓ activo | F2b |
| `hal/pio.h`  | `#error` stub | **F4** (glitch engine) |
| `hal/dma.h`  | `#error` stub | F4 (ADC capture) |
| `hal/usb.h`  | `#error` stub | F3 |

## Estado de drivers

Todos los drivers HW v2.x están implementados. 9 drivers en total:
`ui_leds`, `ui_buttons`, `target_monitor`, `scanner_io`,
`ext_trigger`, `crowbar_mosfet`, `hv_charger`, `emfi_pulse`,
`voltage_mux` (stub `#error`).

## Tests

75 Unity cases en 10 binarios host-tests, todos verde. CI corre
`host-tests` y `fw-release` en paralelo sobre cada push a
`rewrite/v3`.

## Estado USB

`pico_enable_stdio_usb(1)` → CDC default de pico-sdk, VID:PID
`2e8a:000a`, aparece como `/dev/ttyACM*`. DTR debe estar asserted
para que el fw emita. **Sustituido en F3** por composite real:
`1209:FA17` + 4×CDC + vendor IF (CMSIS-DAP v2) + HID IF stub.

## En qué estamos ahora — F3 (USB composite descriptor)

Objetivo: reemplazar stdio_usb default por un descriptor composite
con 10 interfaces (16/16 endpoints). Plan §4.

Entregables F3:
- `usb/include/usb_descriptors.h`
- `usb/src/usb_descriptors.c` con composite completo.
- `usb/src/usb_composite.c` con callbacks TinyUSB.
- Cada CDC (emfi, crowbar, scanner, target-uart) en modo echo.
- Vendor IF responde a `DAP_Info` mínimo.
- HID IF stub.
- `docs/USB_COMPOSITE.md` descriptor anotado.
- VID:PID cambia a `1209:FA17`.
- Diag `stdio_usb` legacy queda en CDC scanner.

Criterios F3:
- Linux: `lsusb -v` muestra 10 interfaces; 4 CDCs como
  `/dev/ttyACM{0..3}`.
- Echo funciona en los 4 CDCs.
- `openocd -f interface/cmsis-dap.cfg -c init` identifica el probe.

Checkpoint F3: 3 OSs si posible, mínimo Linux.

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x, intacto.
- `Hardware/` — KiCad, referencia.
- `third_party/*` — pineados.
- `third_party/faultier/` — **jamás** portar código literal.
- En F3: no empezar F4 antes del tag `v3.0-f3`.

## Reglas de oro

1. **Faseo estricto**: F(N) arranca con tag validado de F(N-1).
2. **HV safety gate ACTIVA**: cualquier commit a
   `drivers/{hv_charger,emfi_pulse}` o `hal/src/rp2040/{pwm,pio}.c`
   (cuando soporten drivers HV) requiere checklist firmado en el
   cuerpo. Plantilla en `docs/SAFETY.md`. F3 **NO** está en safety
   gate — el descriptor USB no toca HV.
3. **`third_party/faultier` sin licencia** — solo referencia.
4. **Drivers no conocen política** — servicios deciden cuándo usar.
5. **Tests antes de commit**.

## Reglas extra activas ahora mismo (F3)

- **Endpoint budget 16/16 al límite**: cualquier feature USB
  adicional requiere sacrificar algo. Plan B del plan §4: eliminar
  HID v1. Plan C: fusionar Target UART dentro de CDC scanner.
- **Validar enumeración antes de servicios**: F3 debe enumerar y
  hacer echo en los 4 CDCs antes de construir servicios encima.
- **`tusb_config.h` completo** antes del descriptor.
- **VID:PID `1209:FA17`** (pid.codes dev). PID oficial se pide en
  F11.
- Diag: portar la lógica de ARM/PULSE/EMFI-fire actual a un shell
  sobre CDC scanner, preservando comportamiento.

---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md` antes de tocar nada. Las 16
> decisiones congeladas del plan §1 **no se relitigan**.

## Fase actual: F2a cerrada → F2b a continuación (drivers HV — SAFETY GATE ACTIVA)

## Tags cerrados en `rewrite/v3`

### `v3.0-f0` — Bootstrap + vendoring + docs + CI (2026-04-23)
Commits: `67e0fdd 5efa2e4 22e767a 6c252fc 88492bb 29000c3 2ca2068`

Submódulos pineados en `third_party/`:
- `pico-sdk@2.1.1` (bddd20f)
- `debugprobe@debugprobe-v2.3.0` (780b827)
- `blueTag@v2.1.2` (887fc83)
- `free-dap@master` (49a30aa)
- `faultier@1c78f3e` — **referencia only, NO compila**
- `cmsis-dap/` — copia headers ARM CMSIS_5@5.9.0

### `v3.0-f1` — HAL + host tests (2026-04-23)
Commits: `6f3c98f 49e8d82 45f3ac9 a7ed586 3512a82`

- `third_party/Unity@v2.6.1` (cbcd08f, MIT) — 6º submódulo.
- `hal/include/hal/{gpio,time}.h` activos.
- `hal/include/hal/{pio,dma,pwm,usb}.h` son `#error` stubs — se
  levantan cuando la fase que los implemente llegue. `adc.h` ya
  levantado en F2a-3.
- `tests/hal_fake/{gpio,time,adc}_fake.c` con probes.

### `v3.0-f2a` — drivers HW low-risk (2026-04-23)
Commits: `33891e1 fb457b3 f500881 6a2ca5f 51dcd53`

Drivers activos:
- `drivers/include/board_v2.h` — pin map autoritativo.
- `drivers/ui_leds` — 3 LEDs + hysteresis HV 500 ms.
- `drivers/ui_buttons` — ARM + PULSE, polaridad normalizada en SW.
- `drivers/target_monitor` — GP29 ADC ch 3 (direct, sin divisor).
- `drivers/scanner_io` — GP0–GP7 (8 canales) con API por índice.
- `drivers/ext_trigger` — GP8, pull configurable.

USB: `stdio_usb` pico-sdk default (VID:PID `2e8a:000a`). Diag
accesible con picocom/screen/pyserial (necesita DTR assert — `cat`
no funciona).

41 Unity cases en 7 binarios, todos verde.

## En qué estamos ahora — F2b (drivers HV)

Orden:
1. `drivers/crowbar_mosfet` — GP16 LP + GP17 HP gate. **Puede** tocar
   HV indirectamente (el MOSFET conmuta la descarga del cap) pero el
   driver en sí solo maneja GPIOs. **Safety checklist opcional** —
   recomendado pero no bloqueante.
2. `drivers/voltage_mux` — **stub** con `#error` (confirmado: no hay
   mux HW en v2.2).
3. `drivers/hv_charger` — GP18/GP20, flyback ~250 V. **SAFETY GATE
   ACTIVA**: commit requiere checklist firmado en el cuerpo.
4. `drivers/emfi_pulse` — GP14 HV pulse vía PIO. **SAFETY GATE
   ACTIVA**. También levanta `hal/pio.h` stub.

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x, intacto hasta merge final.
- `Hardware/` — KiCad, referencia.
- `third_party/*` — pineados; upgrades con commit explícito.
- `third_party/faultier/` — **jamás** portar código literal.
- En F2b: no empezar F3 antes del tag `v3.0-f2b` y validación
  osciloscopio de las rutas HV.

## Reglas de oro

1. **Faseo estricto**: F(N) solo arranca cuando F(N-1) tiene tag
   validado físicamente.
2. **HV safety gate** (ACTIVA a partir de `drivers/hv_charger`):
   cualquier commit que toque `drivers/hv_charger/`,
   `drivers/emfi_pulse/` requiere checklist de safety firmado por el
   maintainer **en el propio mensaje del commit**. Plantilla en
   `docs/SAFETY.md` (se crea en F2b-3).
3. **`third_party/faultier` sin licencia**: solo referencia
   arquitectural. Cero ports literales. Reimplementación desde cero
   con attribution.
4. **Tests antes de commit** en cualquier fase con tests.
5. **Drivers no conocen política** (plan §3). Drivers exponen
   `init/configure/read/write/diag`; los services deciden cuándo
   llamarlos. Ej: `ext_trigger` expone `level()`, NO "rising edge
   detection" — eso es servicio en F5.

## Reglas extra activas ahora mismo (F2b)

- **No** push automático a `origin/rewrite/v3` sin pedirlo al
  maintainer.
- **No** tag `v3.0-f2b` hasta validación física osciloscopio del
  pulso EMFI + comportamiento charger + curva crowbar.
- **No** auto-arm del HV charger en diag — siempre requiere presión
  explícita del botón ARM con timeout de ~60 s.
- **Escudo plástico instalado** antes de cualquier test HV — regla
  del README del proyecto.
- **`docs/SAFETY.md`** nace con el commit de `hv_charger` (F2b-3);
  template de checklist + procedimiento.

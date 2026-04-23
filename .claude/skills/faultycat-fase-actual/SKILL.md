---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md` antes de tocar nada. Las 16
> decisiones congeladas del plan §1 **no se relitigan**.

## Fase actual: F1 cerrada → F2 a continuación (drivers HW)

## Qué está hecho (tags en `rewrite/v3`)

### `v3.0-f0` (2026-04-23) — Bootstrap + vendoring + docs + CI

Commits: `67e0fdd 5efa2e4 22e767a 6c252fc 88492bb 29000c3 2ca2068`
(scaffold, submódulos pineados, CMake/presets/bootstrap, docs, CI,
skill + plan resolutions, fix blink GP10).

Submódulos en `third_party/` (5 pineados):
- `pico-sdk@2.1.1` (bddd20f)
- `debugprobe@debugprobe-v2.3.0` (780b827)
- `blueTag@v2.1.2` (887fc83)
- `free-dap@master` (49a30aa)
- `faultier@1c78f3e` — **referencia only, NO compila**
- `cmsis-dap/` — copia de headers ARM CMSIS_5@5.9.0

### `v3.0-f1` (2026-04-23) — HAL + host tests

Commits: `6f3c98f 49e8d82 45f3ac9 a7ed586 3512a82`
(3 LEDs confirmadas, Unity scaffold, HAL gpio+time, blink reescrito,
CI host-tests job).

Submódulo añadido: `third_party/Unity@v2.6.1` (cbcd08f, MIT).

Estado en código:
- `hal/include/hal/{gpio,time}.h` activos.
- `hal/include/hal/{pio,dma,adc,pwm,usb}.h` son `#error` stubs —
  lift cuando la fase que los implemente llegue.
- `hal/src/rp2040/{gpio,time}.c` — wrappers de pico-sdk.
- `tests/hal_fake/*` — fakes inspeccionables + 10 Unity cases.
- `apps/faultycat_fw/main.c` — blink en HAL, `main.o` sin símbolos
  pico-sdk directos.
- CI corre 2 jobs paralelos: `host-tests` (ctest) + `build` (UF2).

## En qué estamos ahora

Arranque de **F2 — drivers HW** (plan §6 F2). Primera fase que puede
tocar el dominio HV (`drivers/hv_charger`, `drivers/emfi_pulse`,
`drivers/crowbar_mosfet`). La **regla de safety gate activa** para
cualquier commit en esos tres paths: checklist firmado por el
maintainer en el cuerpo del mensaje del commit.

Orden estricto del plan (de bajo riesgo a alto):
1. `ui_leds` + `ui_buttons`
2. `target_monitor` (ADC GP29)
3. `scanner_io` (GP0–GP7)
4. `ext_trigger` (GP8)
5. `crowbar_mosfet` (GP16 LP + GP17 HP)
6. `voltage_mux` (si existe; si no, driver queda stub)
7. **`hv_charger`** (GP18/GP20, HV — safety-first)
8. **`emfi_pulse`** (GP14 PIO — HV)

Cada driver expone `diag <driver>` por UART para testing aislado
antes de USB (F3).

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x, se queda intacto hasta merge
  final.
- `Hardware/` — KiCad, referencia.
- `third_party/*` — pineados. Upgrades solo con commit explícito.
- `third_party/faultier/` — **jamás** portar código literal
  (`LICENSES/NOTICE-faultier.md`).
- En F2: no empezar F3 antes del tag `v3.0-f2` y validación
  osciloscopio de las rutas HV.

## Reglas de oro

1. **Faseo estricto**: F(N) solo empieza cuando F(N-1) está verde con
   tag `v3.0-f(N-1)` confirmado físicamente.
2. **HV safety gate** (ACTIVA a partir de F2): cualquier commit que
   toque `drivers/hv_charger/`, `drivers/emfi_pulse/`, o
   `drivers/crowbar_mosfet/` requiere checklist de safety firmado por
   el maintainer **en el propio mensaje del commit**.
3. **faultier sin licencia**: solo referencia arquitectural. Cero
   ports literales. Reimplementación desde cero con attribution.
4. **Tests antes de commit** en cualquier fase con tests.
5. **Metodología por commits**: commits lógicos pequeños, commit
   final cierra la fase.
6. **Drivers no conocen política** (plan §3). Drivers exponen
   `init/configure/read/write/diag`, los services deciden cuándo
   llamarlos.

## Reglas extra activas ahora mismo (F2)

- **No** push automático a `origin/rewrite/v3` sin pedirlo al
  maintainer.
- **No** tag `v3.0-f2` hasta validación física (osciloscopio HV,
  pulso EMFI, crowbar).
- **HV safety checklist**: plantilla inicial queda en `docs/SAFETY.md`
  (creado en F2 cuando llegue `hv_charger`).

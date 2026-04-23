---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md` antes de tocar nada. Las 16
> decisiones congeladas del plan §1 **no se relitigan**.

## Fase actual: F0 cerrada, esperando tag → F1 a continuación

## Qué está hecho (F0, rama `rewrite/v3`)

Commits F0 (desde `ef82d73`):
- `67e0fdd` scaffold layered tree
- `5efa2e4` vendor deps pineados + LICENSES + CMSIS-DAP headers
- `22e767a` CMake + presets + bootstrap + UF2 blink
- `6c252fc` docs ARCHITECTURE + HARDWARE_V2 + PORTING + README banner
- `88492bb` CI firmware.yml

Submódulos pineados en `third_party/`:
- `pico-sdk@2.1.1` (bddd20f)
- `debugprobe@debugprobe-v2.3.0` (780b827)
- `blueTag@v2.1.2` (887fc83)
- `free-dap@master` (49a30aa)
- `faultier@1c78f3e` — **referencia only, NO compila**

Build verificado local + CI. UF2 release = 12 KB.
Tag `v3.0-f0` **pendiente** de checkpoint físico (blink en v2.2 real).

## En qué estamos

Transición F0 → F1. Esperando:
1. Que el maintainer flashee el UF2 al FaultyCat v2.2 real y confirme
   blink en GP25.
2. Luego crear tag anotado `v3.0-f0` y arrancar F1 (HAL).

## Qué NO tocar en ninguna fase

- `firmware/c/` — firmware legacy, se queda intacto hasta el merge
  final del rewrite. **No borrar, no modificar.**
- `Hardware/` — KiCad v2.x, referencia. Nunca modificar.
- `third_party/*` — vendored, pineados. Upgrades solo con commit
  explícito de bump + razón.
- `third_party/faultier/` — **jamás** portar código literal. Ver
  `LICENSES/NOTICE-faultier.md`.

## Reglas de oro del proyecto

1. **Faseo estricto**: F(N) solo empieza cuando F(N-1) está verde con
   tag `v3.0-f(N-1)` confirmado.
2. **HV safety gate**: cualquier commit que toque
   `drivers/hv_charger/`, `drivers/emfi_pulse/`, o
   `drivers/crowbar_mosfet/` requiere checklist de safety firmado por
   el maintainer **en el propio mensaje del commit**.
3. **faultier sin licencia**: `third_party/faultier/` es *solo
   referencia*. Ningún archivo en este repo puede ser copia, adaptación
   o traducción de sus sources. Reimplementación desde cero con
   attribution en `PORTING.md`.
4. **Tests antes de commit** en cualquier fase que tenga tests (F1+).
5. **Metodología por commits**: fase por fase, 5 commits lógicos
   pequeños, commit final cierra la fase.

## Fase F1 — HAL (la siguiente)

**Entregables previstos** (no empezar hasta tag de F0):
- `hal/include/hal/{gpio,pio,dma,time,usb,adc,pwm}.h` — API portable
- `hal/src/rp2040/*.c` — implementación sobre pico-sdk
- `tests/hal_fake/*` — fakes host-side + unit tests via
  `cmake --preset host-tests`
- El blink de F0 reescrito sobre HAL, sigue blinkando.

**Criterio de cierre:** blink de F0 sobre HAL + tests pasan +
checkpoint físico.

## Reglas extra activas ahora mismo

- No push automático a `origin/rewrite/v3` sin pedírselo al maintainer.
- No tag `v3.0-f0` hasta confirmación de blink físico.

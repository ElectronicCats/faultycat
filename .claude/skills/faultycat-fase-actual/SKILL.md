---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md`, `docs/SAFETY.md` antes de
> tocar nada. Las 16 decisiones congeladas del plan §1 **no se
> relitigan**.

## Fase actual: F6 código completo en `rewrite/v3`, **NO tageado** — verificación física pendiente con osciloscopio

**Estado al momento de pausar:**
- F6-1..F6-6 commiteados + `docs(F6-6)` + 2 fix commits (`fix(F6-5)` echo loop CDC2, `fix(F6-2)` bound spin) + `chore(F6-5)` banner string F5→F6.
- 22/22 host tests verde. fw-release builds clean.
- Físicamente: shell SWD funciona, comandos parsean, recovery OK (FW ya no se cuelga). PERO `swd connect` retorna `NO_TARGET` con un Pico target cableado a CH0/CH1 + GND común — sin osci no se distingue si es bug del PIO program (sospecha principal) o cableado.
- **NO retomar F7 hasta verificar/fixear F6 + tagear `v3.0-f6`**.

**Próxima sesión — protocolo de retake:**
1. BOOTSEL físico + reflash F6 (HEAD del branch).
2. Conectar osci: CH1 → GP0 (SWCLK), CH2 → GP1 (SWDIO), GND clip a GND scanner.
3. `tools/swd_diag.py freq 100` (bajar SWCLK a 100 kHz para ver bits con timebase 100 µs/div).
4. `tools/swd_diag.py connect` mientras osci en single-shot trigger rising-edge CH1.
5. Si SWCLK NO toggleea → bug en sideset / config. Revisar `swd_phy.c::swd_phy_init()` cfg.sideset_*.
6. Si SWCLK toggleea pero SWDIO no envía bits → bug en out pins / shift dir. Revisar `cfg.out_pin_base`, `out_shift_right`.
7. Si todos los bits salen correctos → problema físico (probar cableado con multímetro, GND continuity).

**Si bug en PIO encontrado:** fixear, retest, commit `fix(F6-2): <description>`, después rebase para foldear todos los fix commits dentro de sus features padres, después `git tag v3.0-f6` sobre el commit `docs(F6-6)`.

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

### `v3.0-f5` — Crowbar glitch engine + host proto (2026-04-24)
services/glitch_engine/crowbar/ complete: crowbar_pio (pio0/SM1,
IRQ 1, gate variable LP/HP, width_ns rounded-up), crowbar_campaign
(state machine + break-before-make at arm/fire/teardown). NO HV cap
involved. services/host_proto/crowbar_proto on CDC1 — CRC16-CCITT,
PING reply "F5\0\0", STATUS payload 15 bytes. F2b demo
CROWBAR_CYCLE removed; main runs pump_crowbar_cdc +
crowbar_campaign_tick. usb_composite echo loop bumped to i=2 (CDC0
+ CDC1 owned). tools/crowbar_client.py reference host client. F5-2
+ F5-3 SIGNED. Verified end-to-end on physical board: HP cycle
(200 ns / 10 µs / FIRED), LP cycle (100 ns / FIRED), EMFI
unaffected. 7 HV-SIGNED commits in history.

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
- 4 CDC: emfi(0) → emfi_proto binary, crowbar(1) → crowbar_proto
  binary, scanner(2) diag, target-uart(3) echo.
- Vendor CMSIS-DAP v2 (IF 8) + HID CMSIS-DAP v1 (IF 9) — ambos
  stub (DAP_Info responde, otras DAP commands → DAP_ERROR). F7
  implementa el engine real.
- Windows auto-bind via MS OS 2.0 (WinUSB, GUID compartida con
  debugprobe).
- Magic baud 1200 → BOOTSEL. `tools/flash.sh` lo usa.
- usb_composite_task echo loop arranca en i=2 (CDC2/CDC3 todavía
  echo). Próxima fase que tome CDC2 o CDC3 debe BUMPEAR el i= start.

## Tests

212 Unity cases en 19 binarios, verde. `host-tests` preset + CI.

## En qué estamos ahora — F6 (SWD core, debugprobe port)

Siguiente servicio: portar `third_party/debugprobe/` (MIT, pineado
@v2.3.0) a la arquitectura v3 como `services/swd_core/`. Plan §6 F6.
SWD phy via PIO sobre `pio1` (no compite con glitch engines en
pio0). API interna: `swd_connect`, `swd_read32`, `swd_write32`,
`swd_halt`, `swd_resume`, etc. Comando diag por CDC2 scanner:
`swd probe` → DPIDR del target.

Pautas validadas en F4/F5:
- Service owns PIO program build-from-scratch (o portea desde
  debugprobe upstream que SÍ tiene licencia MIT).
- host_proto/* pattern con CRC16-CCITT framing aún disponible si
  F6 quiere binary protocol; pero el plan dice que SWD se expone
  vía CMSIS-DAP (F7) → no necesita CDC propio.
- usb_composite echo loop NO cambia en F6 (no toma CDC nueva).

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x.
- `Hardware/` — KiCad.
- `third_party/*` — pineados.
- `third_party/faultier/` — **jamás** portar código literal
  (`LICENSES/NOTICE-faultier.md`).
- En F6: no empezar F7 antes del tag `v3.0-f6`.

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

## Reglas extra activas ahora mismo (F6)

- **PIO**: `pio0` ya está saturado de glitch engines (SM0 EMFI / SM1
  crowbar; IRQ 0 EMFI / IRQ 1 crowbar). F6 ARRANCA el uso de `pio1`
  con SWD. Quedan SMs disponibles en pio1 para target-uart y
  scanner (F8). Documentar la asignación en
  ARCHITECTURE.md al cierre.
- **No romper F3/F4/F5**: composite + vendor IF + HID IF siguen
  operando; `pump_emfi_cdc` y `pump_crowbar_cdc` en main.c deben
  seguir vivos. Los host tools existentes (`lsusb`,
  `picocom /dev/ttyACM4`, `tools/emfi_client.py ping`,
  `tools/crowbar_client.py ping`) no deben regresionar.
- **Branch de seguridad**: `rewrite/v3-pre-f5-rebase` quedó del
  rebase de firma F5; borrar con `git branch -D
  rewrite/v3-pre-f5-rebase` cuando confirmes que `v3.0-f5` está
  estable.

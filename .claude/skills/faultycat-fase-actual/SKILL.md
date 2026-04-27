---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md`, `docs/SAFETY.md` antes de
> tocar nada. Las 16 decisiones congeladas del plan §1 **no se
> relitigan**.

## Fase actual: F6 código completo + spec-compliant en `rewrite/v3`, **NO tageado** — bloqueado por HW (TXS0108EPW)

**Estado al cierre de la sesión 2026-04-27 (F6-7):**
- F6-1..F6-6 commiteados + `docs(F6-6)` + serie F6-7 (4 commits — ver abajo).
- 22/22 host tests verde. fw-release builds clean.
- **Bug raíz HW encontrado**: el TXS0108EPW del scanner header rompe SWD push-pull bidireccional (one-shot accelerator clamps HIGH cuando target intenta drivear LOW durante ACK). Validado vs canonical raspberrypi/debugprobe firmware sobre FaultyCat — también falla con `Failed to connect multidrop rp2040.dap0` contra Pi Pico target externo. Documentado en `docs/HARDWARE_V2.md §2`.
- **Workaround SW aplicado**: `swd_phy` emula open-drain en SWDIO (PIO bitloop usa `out pindirs` en vez de `out pins`, pin output preset 0, `swd_phy_write_bits` invierte data). Físicamente: usuario CONFIRMÓ que con OD el target ya drivea SWDIO LOW durante ACK — pero el host sigue leyendo `0b111`. Sospecha: TXS0108E sigue interfiriendo aún en OD, OR bug remanente en read PIO. **Sin diagnosticar más**.
- **NO retomar F7 hasta resolver SWD físico**. Opciones para retake:
  1. **HW bypass**: solder fly wires desde MCU pins (lado A del TXS0108E) directo a header scanner pin, esquivando el chip. La opción limpia.
  2. **HW rev**: reemplazar TXS0108E por 74LVC1T45 con DIR explícito, OR omitir level shifter (ambos lados ya 3.3 V).
  3. Aceptar limitación HW y validar F6 vía OpenOCD en F7 con un debugprobe externo a un Pico target externo (no toca FaultyCat).

### Serie F6-7 (commits añadidos en esta sesión)
1. `fix(F6-7)` HAL `hal_pio_sm_configure` sideset bit_count incluye opt enable bit. Hardcoded 1 (con opt=true) dejaba 0 bits para sideset value → SWCLK nunca toggleaba. Verificado no-impactante para EMFI/crowbar (ambos pasan sideset_pin_count=0).
2. `fix(F6-7)` PIO program instr 1+2 corregidas + open-drain emulation. Cross-verificado contra pioasm sobre upstream `probe.pio`. Tests `test_swd_phy.c` adaptados (exec count 2 = SET PINS,0 + JMP; raw FIFO data invertido).
3. `fix(F6-7)` SWD DP layer — turnaround clock entre header y ACK + dormant-to-SWD wakeup + TARGETSEL multi-drop. API rota: `swd_dp_connect(targetsel, *dpidr)`. Constantes: `SWD_DP_TARGETSEL_RP2040_CORE0/CORE1/RESCUE`.
4. `docs(F6-7)` HARDWARE_V2.md §2 documenta TXS0108E issue.

### Protocolo de retake (próxima sesión)
- Decidir entre HW bypass (#1), HW rev (#2), o avanzar a F7 con caveat (#3).
- Si HW bypass: re-flashear F6 actual (HEAD), conectar bypass, `tools/swd_diag.py connect` debería retornar OK + DPIDR `0x0BC12477`.
- Si OK físico: rebase para foldear los 4 F6-7 commits dentro de F6-1/F6-2 padres, después `git tag v3.0-f6` sobre `docs(F6-7)`.
- Si NO OK físico: scope SWDIO en lado MCU del TXS0108E (lado A), comparar con scanner header (lado B). Distinguir si bypass es definitivo o queda un PIO read bug residual.

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

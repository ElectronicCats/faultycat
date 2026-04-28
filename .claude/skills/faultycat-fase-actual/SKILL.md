---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md`, `docs/SAFETY.md` antes de
> tocar nada. Las 16 decisiones congeladas del plan §1 **no se
> relitigan**.

## Fase actual: F9 — Campaign manager + mutex SWD formal

**Decisión 2026-04-28 (post-tag `v3.0-f8`):** F8 cerrado con smoke
físico verde. F9 promueve el soft-lock shell-level que F8-1 introdujo
(SWD ↔ JTAG) a un `pico-sdk mutex_t` formal que cubra
`daplink_usb` (host externo via CMSIS-DAP) + `glitch_engine/*`
verificación post-glitch + `pinout_scanner` durante scan. Política
estática: `campaign > scanner > daplink_host`.

### F9 — entregables (ver `FAULTYCAT_REFACTOR_PLAN.md §F9`)

- Mutex de bus SWD entre `daplink_usb`, `glitch_engine/*` y
  `pinout_scanner` (pico-sdk `mutex_t`).
- Campaign manager: sweep `(delay, width, power)` con verificación
  SWD post-glitch y streaming de resultados.
- `services/host_proto/campaign_proto/` — streaming binario de
  resultados.
- State machine del mutex documentado en `docs/ARCHITECTURE.md`.

### F9 — criterios

- Campaña real detecta glitches exitosos (target conocido, ej.
  nRF52 APPROTECT bypass de la docu de faultier).
- Mutex bloquea daplink_usb durante una fire window; daplink retorna
  `DAP_ERROR(busy)` → host externo reintenta.
- Tests host: state machine del mutex con prioridad estática
  (campaign preempt scanner preempt daplink_host).

### F6 status — code complete, gate físico bloqueado

Sin cambios desde F8. SWD path no validado físicamente — TXS0108EPW
del scanner header rompe bidirectional push-pull (SWDIO host↔target
durante el ACK window). Workaround SW (open-drain SWDIO emulation)
en `swd_phy.c` activo. Documentado en `docs/HARDWARE_V2.md §2`.

**NO tageado.** `v3.0-f6` se reserva para cuando un bypass HW (fly
wires en el TXS0108E o board rev) deje pasar la validación física.

### F7 status — diferido

Espera HW bypass de F6. Implementar F7 (CMSIS-DAP daplink_usb) sin
F6 físicamente válido es construir sobre una capa SWD no-validable
end-to-end. F8-1's `jtag_clock_bit` ya está listo para que F7 lo
use cuando llegue el momento.

### F8 status — ✓ closed `v3.0-f8` (2026-04-28)

5 sub-fases + 1 polish:
- F8-1: `services/jtag_core/` — CPU bit-bang TAP + IDCODE chain
  (blueTag MIT). 24 host tests.
- F8-2: `services/pinout_scanner/` — P(8,4)=1680 / P(8,2)=56
  permutation iterator + first-match scan. 13 host tests.
- F8-3: shell unificado en CDC2 (`process_shell_line` dispatcher)
  con prefijos `SWD: / JTAG: / SCAN: / SHELL: / BPIRATE: /
  SERPROG:` y placeholder slots para F8-4/F8-5.
- F8-4: `services/buspirate_compat/` — streaming BPv1 BBIO + OOCD
  JTAG sub-mode (no buffer 4KB). `buspirate enter` shell command.
  22 host tests.
- F8-5: `services/flashrom_serprog/` — streaming serprog v1 +
  4-pin CPU SPI bit-bang. `serprog enter` shell command.
  Disconnect detection (DTR drop) → exit. 25 host tests.
- F8-6 polish: 3-read consistency check en `pinout_scan_jtag/swd`
  (rechaza falso positivo `0x6B5AD5AD` observado con RP2040
  parásito en el scanner header), mode-switch trailing-byte fix
  en `pump_shell_cdc` (corrige BBIO1 espurio post-`buspirate
  enter`), `docs/JTAG_INTERNALS.md`.

Smoke físico 2026-04-28: 13/13 checks verde — JTAG init/chain (sin
target → ERR no_target), soft-lock SWD↔JTAG, scan jtag/swd
NO_MATCH bus limpio, BusPirate handshake exacto (5×BBIO1 + OCD1),
serprog NOP/Q_*/SPIOP, disconnect detection, F4/F5 ping
regression, F3 BOOTSEL.

**No verificado físicamente** (falta target externo): JTAG IDCODE
contra STM32/ESP32, OpenOCD vía BusPirate end-to-end, flashrom
contra 25-series real. `scan swd` inherits F6 TXS0108E HW gate.

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
HID CMSIS-DAP v1). 16/16 endpoints. BOS + MS OS 2.0 for Windows
WinUSB auto-bind. Magic baud 1200 on any CDC triggers
`reset_usb_boot`.

### `v3.0-f4` — EMFI glitch engine + host proto (2026-04-24)
hal/pio + hal/dma lifted. `services/glitch_engine/emfi/` complete.
`host_proto/emfi_proto` on CDC0 (CRC16-CCITT). 100 ms HV invariant
gate. `tools/emfi_client.py`.

### `v3.0-f5` — Crowbar glitch engine + host proto (2026-04-24)
`services/glitch_engine/crowbar/` complete (pio0/SM1, IRQ 1).
`host_proto/crowbar_proto` on CDC1. F2b CROWBAR_CYCLE removed —
operator owns gate via crowbar_proto. `tools/crowbar_client.py`.
Verified end-to-end on physical board: HP/LP cycles fire correctly,
EMFI unaffected. 7 HV-SIGNED commits.

### `v3.0-f8` — JTAG / scanner / BusPirate / serprog (2026-04-28)
`services/{jtag_core, pinout_scanner, buspirate_compat,
flashrom_serprog}` all from blueTag@v2.1.2 MIT. Unified CDC2 shell
with mode-switch into binary protocols. F6/F7 still gated; F8 path
via JTAG (unidirectional push-pull) is not blocked by the TXS0108E.
False-positive guard (3-read consistency) on the scanner. Smoke
13/13 verde. 26 host-test binaries / 347 cases.
`docs/JTAG_INTERNALS.md` documenta el wire stack completo.

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
- 4 CDC:
  - CDC0 emfi → `emfi_proto` binary (F4)
  - CDC1 crowbar → `crowbar_proto` binary (F5)
  - CDC2 scanner → unified shell (F6 SWD + F8-1..F8-5
    JTAG/scan/buspirate/serprog) — diag snapshot stream gagged
    durante modos binarios
  - CDC3 target-uart → echo (futuro target-UART passthru)
- Vendor CMSIS-DAP v2 (IF 8) + HID CMSIS-DAP v1 (IF 9) — ambos
  todavía stub (DAP_Info responde). F7 implementa el engine real
  cuando F6 desbloquee físicamente.
- Windows auto-bind via MS OS 2.0 (WinUSB, GUID compartida con
  debugprobe).
- Magic baud 1200 → BOOTSEL. `tools/flash.sh` lo usa.
- `usb_composite_task` echo loop arranca en `i=3` (CDC0/1/2
  owned, CDC3 todavía echo). Próxima fase que tome CDC3 debe
  bumpear el `i=` start.

## Estado de servicios

- `services/glitch_engine/{emfi, crowbar}/` — F4 / F5.
- `services/swd_core/` — F6 (code complete, no tag).
- `services/host_proto/{emfi_proto, crowbar_proto}/` — F4 / F5.
- `services/jtag_core/` — F8-1.
- `services/pinout_scanner/` — F8-2 + F8-6 stability check.
- `services/buspirate_compat/` — F8-4.
- `services/flashrom_serprog/` — F8-5.
- `services/daplink_usb/` — F7 (todavía stub).
- `services/host_proto/campaign_proto/` — F9 entregable.

## Tests

26 Unity binarios / 347 cases / 100% verde. `host-tests` preset + CI.
Plus `hal_fake_gpio` edge sampler + per-pin input scripts (F8-1
infra, reusable para SPI/serial/JTAG-style clocked-bus tests).

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x.
- `Hardware/` — KiCad.
- `third_party/*` — pineados.
- `third_party/faultier/` — **jamás** portar código literal
  (`LICENSES/NOTICE-faultier.md`).
- En F9: no tocar el wire layer SWD (espera F6 HW gate); no
  cambiar el shell prefix conventions (`SHELL: SWD: JTAG: SCAN:
  BPIRATE: SERPROG:`) sin actualizar `tools/{swd,jtag,scanner}_diag.py`.

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

## Reglas extra activas ahora mismo (F9)

- **PIO** sigue: pio0 saturado (EMFI SM0 / crowbar SM1).
  pio1 SM0 swd_phy. SM1..3 disponibles para target-uart y futuros.
- **No romper F4/F5/F8**: composite + glitch engines + shell + binary
  modes deben seguir operando. Los smoke tools de F8-6 son
  referencia: `tools/{swd,jtag,scanner}_diag.py`,
  `tools/{emfi,crowbar}_client.py`, `tools/flash.sh`,
  `tools/bootsel.sh`.
- **Mutex F9 ≠ shell soft-lock F8**. F8-1 protege solo entre swd y
  jtag a nivel de comandos del shell. F9 lo extiende a:
  - daplink_usb (F7) cuando llegue.
  - glitch_engine post-fire SWD verification.
  - pinout_scanner durante scan.
  Política prioridad estática `campaign > scanner > daplink_host`.
  daplink retorna `DAP_ERROR(busy)` cuando otro consumer tiene el
  bus → host externo reintenta.
- **F9 no toca el wire SWD**. Si necesitás validar contra HW real,
  tenés que esperar el bypass del TXS0108E o usar loopback con
  debugprobe externo + Pico target externo (no scanner header).

## Protocolo de retake F6 (cuando llegue HW fix)

1. HW bypass (fly wire) o board rev disponible → reflash actual
   `rewrite/v3` HEAD.
2. `tools/swd_diag.py connect` debería retornar `OK connect
   dpidr=0x0BC12477` contra un Pi Pico target.
3. Si OK → rebase para foldear los 4 F6-7 commits dentro de
   F6-1/F6-2 padres → `git tag v3.0-f6`.
4. Después F7 (CMSIS-DAP).
5. Después re-tag F9 con SWD path validado.

## Branches huérfanos

- `rewrite/v3-pre-f5-rebase` — del rebase de firma F5; borrar con
  `git branch -D rewrite/v3-pre-f5-rebase` cuando confirmes que
  `v3.0-f5` está estable. **TODO de Sabas**.

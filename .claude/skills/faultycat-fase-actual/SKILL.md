---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# FaultyCat v3 — fase actual

> **Siempre** leer `FAULTYCAT_REFACTOR_PLAN.md`, `docs/ARCHITECTURE.md`,
> `docs/HARDWARE_V2.md`, `docs/PORTING.md`, `docs/SAFETY.md` antes de
> tocar nada. Las 16 decisiones congeladas del plan §1 **no se
> relitigan**.

## Estado en curso de F11-0 (2026-04-29)

| sub-fase | estado | commit |
|----------|--------|--------|
| F11-0a EMFI control modal | ✓ smoke verde | `876124f` + `24b38b4` (fix) |
| F11-0b Crowbar control modal + SharedSerial CDC1 race fix | ✓ smoke verde | `135b32f` |
| F11-0c Campaign control modal MVP (engine=crowbar) | ✓ code+tests; smoke pendiente | `9640656` |
| F11-0d Scanner control modal | pending | — |
| F11-0e Target UART panel CDC3 | pending | — |
| F11-0f Reflash via magic baud 1200 | pending | — |
| F11-0g Help modal `?` | pending | — |
| F11-0h CDC ownership + diag-mute indicator | pending | — |
| F11-0i Lockfile concurrencia TUI ↔ CLI | pending | — |
| F11-0j Hardening (USB disconnect / multi-term / resize) | pending | — |
| F11-0k tests + docs(F11-0) + tag `v3.0-f11-0` | pending | — |

Decisiones MVP que afectan scope:
- **F11-0c engine multiplex deferido a F-future.** El modal Campaign
  soporta engine=crowbar only; `emfi` aparece deshabilitado en el
  Select con label "(emfi multiplex: F-future — needs Connections
  refactor)". Razón: necesitaría wrappear CDC0 con SharedSerial,
  retrofit EmfiClient para `serial_factory`, agregar
  `campaign_emfi`, y patchear el race CDC0 latente en F11-0a
  callbacks. ~30-40 min adicionales por <20% del valor; el operador
  típicamente itera sobre crowbar (donde está el ext-trigger
  jumper).
- **`s` rebound de `toggle_demo` (locked 6-step) a
  `stop_sweep_express`** — express-stop sin abrir modal. El concepto
  de demo locked desaparece; `p` abre el modal Campaign con sweep
  custom.

Push pendiente: 7 commits + tag `v3.0-f10`. El maintainer pushea
manualmente cuando puede (HTTPS creds no disponibles desde shell
del agente).

Memorias relevantes para esta fase:
- `project_f11_f12_scope_expansion.md` — scope expansion F11/F12.
- `feedback_shared_serial_race.md` — patrón SharedSerial+lock,
  surgió en F11-0b smoke.
- `feedback_smoke_before_tag.md` — smoke obligatorio antes del
  `v3.0-f11-0` tag.

---

## Fase actual: F11 — TUI complete control + Hardening + Release v3.0.0

**Decisión 2026-04-29 (post-tag `v3.0-f10` + smoke interactivo TUI):**
F10 cerrado con scope honesto = monitor + 6-step crowbar LP demo
locked. NO es UI de control completa (arm/fire/disarm directos,
sweep custom, SWD/JTAG operations, target serial, reflash desde
TUI — todo eso vive en CLI). El §F10 entregables prometía "100%
del faultycmd viejo"; lo entregado es subset. **F11 abre con
F11-0 (TUI complete control surface, 11 sub-fases)** antes del
docs/benchmarks/release polish original.

**Decisión 2026-04-29 (paralela):** post `v3.0.0` se abre fase
nueva **F12 — GUI Web local v3.1.0**. Audiencia: operadores
técnicos + trainees + conferencias + classroom (broader que
CLI/TUI hackers). Stack frozen: FastAPI + Tailwind + Alpine.js +
Chart.js dentro de `host/faultycmd-py/` como `faultycmd.gui`
submódulo (single install). NO entra a v3.0.0 — es su propio
release.

### F11 — entregables (ver `FAULTYCAT_REFACTOR_PLAN.md §F11`)

**F11-0 — TUI complete control surface (11 sub-fases):**

- F11-0a EMFI control modal (configure/arm/fire/disarm + capture
  Rich table + last-config + HV confirm).
- F11-0b Crowbar control modal (configure LP+HP/arm/fire/disarm
  + last-config).
- F11-0c Campaign control modal (full sweep params, no demo
  locked).
- F11-0d Scanner control modal (SWD r32/w32/freq/reset + JTAG
  reset/trst + scan progress + pin assignment).
- F11-0e Target UART panel CDC3 (passthrough mini-terminal).
- F11-0f Reflash action (magic baud 1200 BOOTSEL + UF2 picker +
  reabrir CDCs).
- F11-0g Help modal (`?`) — hotkeys + ownership + safety.
- F11-0h CDC ownership + diag-mute indicator en footer.
- F11-0i Lockfile concurrencia TUI ↔ CLI (~/.cache/faultycmd/
  cdc-N.lock con owner tag + `--force` escape hatch).
- F11-0j Hardening: USB disconnect, multi-terminal smoke
  (alacritty/kitty/gnome-terminal/tmux), terminal resize.
- F11-0k tests + docs(F11-0) + tag `v3.0-f11-0`.

**F11-1..F11-7 — Release polish (original):**

- F11-1: docs/ sweep + gap fill (ARCHITECTURE, HARDWARE_V2,
  USB_COMPOSITE, PROTOCOLS, SWD_INTERNALS, JTAG_INTERNALS,
  MUTEX_INTERNALS, SAFETY, PORTING). README top-level apunta a
  `host/faultycmd-py/`.
- F11-2: benchmarks reproducibles — trigger→pulso latency
  (EMFI + crowbar), SWD throughput (gated by F6 unblock),
  swd_bus_lock acquire/release overhead. `tools/bench_*.py` +
  `docs/PERFORMANCE.md`.
- F11-3: HV safety review v2 — segunda pasada de SAFETY.md con
  lecciones F2b/F4/F5/F9 acumuladas, firma final maintainer.
- F11-4: CHANGELOG + `docs/MIGRATION_FROM_V2.md` (5 wire-protocol
  cambios desde v2.x).
- F11-5: archive `tools/{emfi,crowbar,campaign}_client.py` +
  `tools/{swd,jtag,scanner}_diag.py` → `tools/legacy/`.
- F11-6: GitHub release `v3.0.0` (UF2 + tarball + pyinstaller
  Linux). Tag `v3.0.0`.
- F11-7: docs(F11) + tag `v3.0-f11` (intermedio antes de
  `v3.0.0`).

### F11 — criterios

- TUI cubre 100% del faultycmd legacy + campañas custom + switch
  entre engines (no solo monitor; eso era el gap de F10).
- README top-level apunta a `host/faultycmd-py/` como host tool
  oficial.
- Migration guide cubre los 5 wire-protocol cambios respecto al
  firmware v2.x (VID/PID 1209:FA17, magic baud 1200, frame format
  CRC16-CCITT, opcodes EMFI/CROWBAR/CAMPAIGN, shell scanner CDC2).
- Benchmarks reproducibles: script + tabla en
  `docs/PERFORMANCE.md`.
- `v3.0.0` tag con summary completo de F0..F11.

### F12 — GUI Web local (v3.1.0, post `v3.0.0`)

Track abierto post-3.0. Stack: FastAPI ASGI + WebSocket + Tailwind
+ Alpine.js + Chart.js + xterm.js (target serial). Reusa 100% de
`faultycmd.protocols.*` / `framing` / `usb`. `faultycmd gui` arranca
server local + abre browser localhost:8080. Sub-fases F12-1..F12-9.
Defer a v3.1.x: capture trace plot, A/B compare campaigns, saved
sweeps history, i18n, replay step, step-debug. Ver §F12.

### F10 status — ✓ closed `v3.0-f10` (2026-04-29)

7 sub-fases:
- F10-1: `host/faultycmd-py/` skeleton — `pyproject.toml`
  (hatchling), `faultycmd.framing` (SOF=0xFA + CRC16-CCITT poly
  0x1021 init 0xFFFF), `faultycmd.usb` (VID:PID 1209:FA17 +
  udevadm `ID_USB_INTERFACE_NUM` mapper). 22 host tests.
- F10-2: `faultycmd.protocols.{_base, emfi, crowbar, campaign}` —
  binary CDC clients. `BinaryProtoClient` base con
  `serial_factory` hook (testable con `FakeSerial` fixture sin
  hardware). 35 nuevos tests = 57.
- F10-3: `faultycmd.protocols.scanner` — text-shell wrapper sobre
  CDC2 (no es BinaryProtoClient — el shell es line-based).
  `ACCEPTED_PREFIXES` filter + send_line/send_line_collect. SWD/
  JTAG/SCAN/BusPirate/serprog wrappers. 15 nuevos tests = 72.
- F10-4: `faultycmd.cli` — `click` command groups (info/emfi/
  crowbar/campaign/scanner/tui) + Rich Console tables + Live
  view para `campaign watch`. `_wrap_main` exception handler.
- F10-5: `faultycmd.tui` — Textual 4-panel dashboard (HV/trigger/
  SWD/campaign). `DiagSnapshot` regex sobre el stream CDC2
  `print_snapshot()` line del firmware. `SharedSerial` wrapper
  para Crowbar+Campaign multiplex en CDC1. BINDINGS=q/r/c/s. 13
  panel-state tests (Pilot evitado por incompat con threading
  setup). 85 total tests.
- F10-6: packaging — `.github/workflows/host-py.yml` (lint/test
  matrix Py 3.10/3.11/3.12 + build-binary job pyinstaller),
  `__main__.py` para `python -m faultycmd`, ruff cleanup
  (73 → 0 issues; 64 autofix + 9 manual incl. B007/E702/N802/
  N818).
- F10-7: docs(F10) + smoke + tag.
- **F10-polish** (smoke interactivo TUI 2026-04-29): `FaultycmdTUI`
  asignaba `self._workers` clobbering Textual's `App._workers`
  (WorkerManager). Static panels crash en unmount con
  `'list' has no cancel_node'`, daemon threads cascade-fail con
  `RuntimeError: Event loop is closed`. Fix: rename a
  `_poll_threads` + helper `_post()` (`try/except RuntimeError`
  defensivo en 8 daemon call_from_thread sites) + regression test
  guard. 86 total tests.

**Override formal de §1 #6** (commit `0a34a22`, 2026-04-28): plan
original especificaba Rust workspace + ratatui. Sabas confirmó
Python + Textual + Rich por team familiarity + reuso directo de
los 4 reference clients Python + onboarding más bajo. Wire
protocols **no cambian**. Memoria
`project_faultycmd_python_override.md` documenta la decisión.

Smoke físico 2026-04-29 sobre v2.2: CLI 18 checks (info / emfi
ping/status/configure / crowbar ping/status/configure LP+HP /
campaign status both engines / 3-step + 50-step sweep + stop
mid-sweep + drain stopped / scanner SWD init+connect+deinit /
JTAG init+chain+idcode+deinit / scan-jtag 1680 perms NO_MATCH /
scan-swd 56 perms NO_MATCH) — todos verde, expected gates en
SWD connect (TXS0108E) y JTAG chain (no target). TUI 12 checks
(launch + 4 paneles populate + diag CDC2 stream con ADC
fluctuando + EMFI/Crowbar/Campaign tail correcto + hotkeys `s`
toggle demo + `c` clear log + `s` mid-sweep stop con sweep
largo disparado por CLI paralelo + `r` reconnect sin freeze +
`q` quit sin traceback) — todos verde post F10-polish.
86/86 host-py tests verde + 404/404 firmware Unity verde.
CI YAML well-formed.

**No verificado físicamente** (reusa F6 gates): SWD verify hook
real (F6 HW-blocked) y CMSIS-DAP path (F7 deferido).

### F9 status — ✓ closed `v3.0-f9` (2026-04-28)

5 sub-fases + 1 polish:
- F9-1: `services/swd_bus_lock/` — service-layer mutex sobre flag
  volátil + owner tag (4 tags: IDLE/CAMPAIGN/SCANNER/DAPLINK). 13
  host tests. Coexiste con F8-1 shell soft-lock — distintas capas.
- F9-2: `services/campaign_manager/` — 6-state machine + cartesian
  sweep generator + 256-entry × 28 B ringbuffer + pluggable step
  executor. Default executor es no-op (lets host tests drive sin
  motores). 27 host tests.
- F9-3: engine adapters in `apps/faultycat_fw/main.c` —
  `campaign_executor_emfi/_crowbar` blocking-with-cooperative-
  yield. Verify hook acquires/releases swd_bus_lock alrededor de
  un no-op call (F-future plugs real SWD post-fire verify cuando
  F6 unblock). Shell `campaign demo crowbar / status / drain /
  stop` para smoke en CDC2.
- F9-4: `services/host_proto/campaign_proto/` — opcodes
  `CAMPAIGN_CONFIG/START/STOP/STATUS/DRAIN` (0x20..0x24)
  multiplex en CDC0 (EMFI) / CDC1 (crowbar). 17 host tests.
  **Polish requerido**: `CROWBAR_PROTO_MAX_PAYLOAD` era 64 B,
  bumped a 512 (DRAIN replies caían silentamente al guard); también
  `pump_emfi/crowbar_cdc reply[768]` ahora `static` (defensivo vs
  stack overflow en deep executor wait loops).
- F9-5: `tools/campaign_client.py` pyserial CLI mirror de
  emfi/crowbar_client.py. 30 ms gap intencional en watch loop
  para race en dispatch ordering durante executor wait.
- F9-6: `docs/MUTEX_INTERNALS.md` documenta el wire stack F9
  completo + smoke results.

Smoke 2026-04-28: `campaign demo crowbar` shell + `campaign_client.py
configure → start → watch` ambos completan sweeps end-to-end.
Mutex acquire/release sin deadlock. Result streaming exacto.

**No verificado físicamente**: SWD verify hook real (F6 gated) y
detect-glitch-success contra target real. Killer feature
**partially validated** — infra funciona; falta SWD verify post-
fire.

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

### `v3.0-f9` — Campaign manager + SWD bus mutex (2026-04-28)
`services/swd_bus_lock/` — service-layer cooperative mutex (4 owner
tags, 13 tests). `services/campaign_manager/` — 6-state machine +
cartesian sweep + 256×28 B ringbuffer + pluggable executor (27
tests). `apps/faultycat_fw/main.c` — engine adapters
(emfi/crowbar) blocking-with-cooperative-yield + verify hook (no-op
until F6 unblocks SWD physically). Shell `campaign <subcmd>` (smoke
demo). `services/host_proto/campaign_proto/` — opcodes 0x20..0x24
multiplex en CDC0/CDC1 (17 tests). `tools/campaign_client.py`
pyserial CLI. Polish: `CROWBAR_PROTO_MAX_PAYLOAD` 64 → 512
(silent-drop bug); pump_emfi/crowbar_cdc reply[768] static
(stack-overflow defensa). 29 binarios / 404 cases.
`docs/MUTEX_INTERNALS.md` documenta el wire stack completo +
smoke 2026-04-28: `campaign demo crowbar` + `campaign_client.py
watch` ambos completan sweeps end-to-end. **Killer feature
partially validated** — sweep + result streaming físicamente
verde; SWD verify post-fire gated by F6.

### `v3.0-f10` — faultycmd Python host tool (2026-04-29)
Override formal de §1 #6 (commit `0a34a22`): Rust+ratatui →
Python+Textual+Rich. `host/faultycmd-py/` package monorepo
(`pyproject.toml` hatchling). 7 sub-fases F10-1..F10-7:
framing/usb/protocols.{emfi,crowbar,campaign,scanner}/cli/tui +
CI matrix Py 3.10/3.11/3.12 + pyinstaller binary job. 86 host-py
tests (pytest, incluye F10-polish regression guard) + ruff clean.
Smoke físico v2.2: CLI 18 checks (info / ping/status emfi+crowbar
/ configure cycles / 3-step + 50-step + stop-mid sweep / scanner
SWD+JTAG init+deinit + scan-jtag NO_MATCH 1680 perms + scan-swd
NO_MATCH 56 perms) verde. TUI 12 checks (launch + paneles +
multiplex CDC1 + hotkeys s/c/r/q + stop mid-sweep) verde post
F10-polish (rename `_workers` → `_poll_threads` que clobbeaba
Textual `App._workers` WorkerManager + `_post()` defensivo
contra el daemon-thread / asyncio-loop shutdown race). Wire
protocols **no cambian** —
solo el host language. Memory `project_faultycmd_python_override.md`
documenta la decisión.

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
- `services/host_proto/{emfi_proto, crowbar_proto}/` — F4 / F5,
  extended con CAMPAIGN_* opcodes en F9-4.
- `services/jtag_core/` — F8-1.
- `services/pinout_scanner/` — F8-2 + F8-6 stability check.
- `services/buspirate_compat/` — F8-4.
- `services/flashrom_serprog/` — F8-5.
- `services/swd_bus_lock/` — F9-1.
- `services/campaign_manager/` — F9-2 (+ engine adapters in apps/
  via F9-3).
- `services/host_proto/campaign_proto/` — F9-4.
- `services/daplink_usb/` — F7 (todavía stub, gated by F6 HW).

## Tests

Firmware: 29 Unity binarios / 404 cases / 100% verde. `host-tests`
preset + CI (`firmware.yml`). Plus `hal_fake_gpio` edge sampler +
per-pin input scripts (F8-1 infra, reusable para SPI/serial/JTAG-
style clocked-bus tests).

Host: 86 pytest cases en `host/faultycmd-py/` (framing/usb/
protocols/cli/tui state + F10-polish regression guard contra
shadowing del Textual `App._workers`) / 100% verde. CI matrix Py
3.10/3.11/3.12 en `host-py.yml` + ruff lint + pyinstaller binary
build job.

Total: **490 tests verde** end-to-end.

## Qué NO tocar

- `firmware/c/` — firmware legacy v2.x.
- `Hardware/` — KiCad.
- `third_party/*` — pineados.
- `third_party/faultier/` — **jamás** portar código literal
  (`LICENSES/NOTICE-faultier.md`).
- En F11: no tocar el firmware estructural (F4..F9 tagged) ni
  el host tool funcional (F10 tagged). F11 es docs + benchmarks +
  release polish, no nuevas features. Si surge un bug regresivo
  durante el bench, es F-future fix, no F11.

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

## Reglas extra activas ahora mismo (F11)

- **F11 es polish + release**, no nuevas features. No agregar
  servicios, comandos CLI/TUI nuevos, ni opcodes wire-protocol. Si
  algo realmente urgente aparece, va a F-future después de v3.0.0.
- **Wire protocol congelado para v3.0.0**: emfi_proto /
  crowbar_proto / scanner shell (CDC2) / campaign_proto (CDC0+CDC1
  opcodes 0x20..0x24) son la API pública. Cualquier cambio rompe
  faultycmd ↔ firmware compatibility — y eso es post-3.0.
- **Archive de tools/ legacy**: `tools/{emfi,crowbar,campaign}_client.py`
  + `tools/{swd,jtag,scanner}_diag.py` se moverán a `tools/legacy/`
  o se borran cuando faultycmd cubra el 100% de los flujos.
  Decisión final con Sabas antes de borrar — útiles para A/B testing
  contra el client Python si algo se desvía.
- **Safety review HV**: el path EMFI/crowbar lleva 9 commits SIGNED
  + smoke físico repetido sin incidente desde F2b. Segunda pasada
  de SAFETY.md debe documentar las lecciones acumuladas (HV invariant
  100ms, break-before-make, BUTTON_PULSE como single owner, etc.).
- **Benchmarks obligatorios**: tabla en `docs/PERFORMANCE.md` con
  números reales medidos (no estimados): trigger→pulso latency,
  campaign sweep throughput, mutex acquire/release overhead.
  Reproducible vía `tools/bench_*.py`.
- **No romper F0..F10**: 29 firmware Unity binarios / 404 cases +
  86 host-py tests = 490 tests verde. Smoke físico verde sobre
  v2.2. Cualquier regresión observada durante el bench apunta al
  cambio que se acaba de meter.

## Protocolo de retake F6 (cuando llegue HW fix)

1. HW bypass (fly wire) o board rev disponible → reflash actual
   `rewrite/v3` HEAD.
2. `tools/swd_diag.py connect` debería retornar `OK connect
   dpidr=0x0BC12477` contra un Pi Pico target.
3. Si OK → rebase para foldear los 4 F6-7 commits dentro de
   F6-1/F6-2 padres → `git tag v3.0-f6`.
4. Después F7 (CMSIS-DAP) — adopta `swd_bus_lock` con prioridad
   `daplink_host` (replica plan §4 contract: DAP_ERROR busy on
   contention).
5. Después wire `swd_dp_read32` real al campaign_manager verify
   hook en `apps/faultycat_fw/main.c::campaign_dispatch_executor`
   (una línea — el hook ya está armado). Re-smoke con un target
   real — no requiere re-tag F9.
6. Después F8-2 scanner adopta `swd_bus_lock(SCANNER)` per
   candidate (cleanup mostly, hoy no hay race reachable por shell).

## Branches huérfanos

Limpio (2026-04-28). `rewrite/v3-pre-f5-rebase` borrado local
después de confirmar tree-equality con tag `v3.0-f5` (mismo árbol
`03e87b29...`, diff vacío). Si todavía aparece
`origin/rewrite/v3-pre-f5-rebase` en `git fetch`, correr
`git push origin --delete rewrite/v3-pre-f5-rebase` desde un shell
con credenciales GitHub configuradas.

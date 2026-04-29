# FaultyCat v3 — Plan Maestro (v3 final, corregido)

> Rewrite total desde cero del firmware de FaultyCat sobre el **hardware v2.x existente** (sin diseñar board nuevo). Arquitectura en capas, dependencias vendorizadas, multi-ataque (EMFI + crowbar), pinout scanner (blueTag), CMSIS-DAP expuesto (debugprobe), USB composite multi-CDC, host tool en Rust con TUI.

> **Nota de versioning:** el firmware se numera `v3.0` por el rewrite; el hardware sigue siendo `v2.1`/`v2.2`. Son versiones independientes.

---

## 1. Decisiones congeladas

| # | Decisión | Valor |
|---|----------|-------|
| 1 | "daplink" | **DAPLink / CMSIS-DAP** (protocolo ARM de referencia) |
| 2 | Refactor | **Rewrite total desde cero**, arquitectura en capas |
| 3 | Dependencias | **Vendored total** en `third_party/`, submódulos pineados |
| 4 | Integración SWD | **Dual:** CMSIS-DAP expuesto por USB + hooks internos al glitch engine |
| 5 | MCU del propio device | **RP2040** únicamente |
| 6 | Host tool `faultycmd` | **Python** package — `click` CLI + **Textual** TUI + **Rich** styled output. *(revisited 2026-04-28 — see §F10 override block; original spec was Rust workspace + ratatui)* |
| 7 | Features extra | **Solo SWD** (blueTag trae JTAG + multi-protocolo gratis) |
| 8 | Backward compat | **Romper si hace falta** (firmware nuevo, protocolo nuevo) |
| 9 | Hardware dev | **FaultyCat v2.x real + target disponibles** |
| 10 | Base del glitch engine | **faultier (hextree)** — crowbar, mux, trigger |
| 11 | Base del pinout scanner | **blueTag (Aodrulez)** — JTAGulator + multi-protocol |
| 12 | CMSIS-DAP implementation | **debugprobe (RPi)** primario, **free-dap (ataradov)** solo referencia |
| 13 | Base EMFI/voltage/scanner legacy | **faultycat/firmware/c** (HV charger, EMFI pulse, voltage glitching actual, scanner v2.x) |
| 14 | Separación USB CDCs | **Por tipo de ataque:** CDC `emfi` + CDC `crowbar` (+ `scanner` + `target-uart`) |
| 15 | blueTag en el HW | **Usa el pinout dedicado que YA existe en v2.x** (scanner JTAG/SWD introducido en v2.1). No requiere HW nuevo. |
| 16 | Hardware target del rewrite | **FaultyCat v2.x** (v2.1 / v2.2). **No se diseña board v3.** Firmware v3 corre sobre HW v2.x. |

### Qué provee el HW v2.x (según README oficial y store Electronic Cats)

El **v2.1** introdujo respecto al v1.x:
- Voltage glitching (MOSFET integrado para crowbar).
- Trigger usando pines dedicados (pinout nuevo).
- Trigger voltage reference (referencia para voltage glitching preciso).
- Analog input para monitorear el estado del target.
- **JTAG/SWD scanner** (pinout dedicado para scanner tipo JTAGulator).

El **v2.2** es un hardware update menor sobre v2.1. **v2.2 es el único
hardware que salió a producción** (confirmado por el maintainer el
2026-04-23); v2.1 → v2.2 fue cambio de etiquetas, no de nets. El
firmware v3.0 targets v2.2 explícitamente; el pinout documentado en
`docs/HARDWARE_V2.md` aplica tal cual a ambas revisiones.

**Consecuencia:** todo el hardware necesario para EMFI + crowbar + scanner + trigger + target-monitor ya está en el board. El número exacto de canales del scanner se documentará en F0 leyendo el KiCad.

---

## 2. Repos base y cómo se combinan

| Repo | Licencia | Aporta | Ubicación en `third_party/` |
|------|----------|--------|----------------------------|
| `raspberrypi/pico-sdk` | BSD-3 | HAL RP2040, tinyusb wrapper, build system | `pico-sdk/` (submódulo, tag fijo) |
| `hathach/tinyusb` | MIT | USB stack (traído por pico-sdk) | via pico-sdk |
| `ARM-software/CMSIS_5` | Apache-2.0 | headers CMSIS-DAP | `cmsis-dap/` (copia de headers) |
| `raspberrypi/debugprobe` | MIT | **CMSIS-DAP v2 sobre RP2040 (primario)** — PIO SWD, PIO UART | `debugprobe/` (submódulo ref) |
| `ataradov/free-dap` | MIT | CMSIS-DAP referencia cruzada | `free-dap/` (submódulo ref) |
| `Aodrulez/blueTag` | MIT | **JTAGulator + multi-protocolo** — scanner, BusPirate OpenOCD, flashrom serprog | `blueTag/` (submódulo ref) |
| `hextreeio/faultier` | **NONE** (verified 2026-04-23 — "all rights reserved" por defecto) | **Arquitectura de glitcher** (reference-only, NO port literal) — crowbar MOSFET, voltage mux, trigger externo, SWD de verificación | `faultier/` (submódulo ref, `EXCLUDE_FROM_ALL`) |
| `ElectronicCats/faultycat` | CC-BY-SA-3.0 (HW) | **HV charger + EMFI pulse + voltage glitching v2.1 + scanner v2.x** (el firmware C actual ya tiene mucho de esto) | rama `main` del propio repo para consulta histórica |

### Cómo se combinan (mental model)

- **faultier** es la columna vertebral de la arquitectura moderna del glitcher ("configure → arm → fire con trigger externo → verificar por SWD"). Portamos su arquitectura a la nueva estructura de capas.
- **faultycat/firmware/c** (el firmware actual v2.x) es la referencia crítica porque ya implementó voltage glitching y scanner sobre el pinout real del v2.x. Hay que leerlo antes de portar para no reinventar el pinout.
- **blueTag** se porta como servicio `pinout_scanner` + `buspirate_compat` + `flashrom_serprog`, usando el pinout de scanner que ya trae el v2.x (decisión 15).
- **debugprobe** se porta como `swd_core` (capa física SWD) + `daplink_usb` (CMSIS-DAP v2 vendor IF + HID v1 fallback).
- **free-dap** queda como referencia para contrastar implementación cuando haya dudas, sin build.

### Licencias

Todas compatibles entre sí (MIT, BSD-3, Apache-2.0). El firmware nuevo bajo **BSD-3-Clause** (recomendado, estándar embedded, compatible con todos los deps). La licencia CC BY-SA 3.0 del HW del FaultyCat original se conserva para la parte hardware (KiCad), no para firmware.

---

## 3. Arquitectura objetivo

```
faultycat/
├── apps/
│   └── faultycat_fw/               # entrypoint único (main)
│       ├── main.c
│       └── CMakeLists.txt
│
├── services/                       # lógica de alto nivel
│   ├── glitch_engine/
│   │   ├── emfi/                   # EMFI (faultycat origin, HV pulse)
│   │   └── crowbar/                # voltage glitching (faultier + faultycat v2.1)
│   ├── swd_core/                   # debugprobe-derived
│   ├── jtag_core/                  # blueTag-derived
│   ├── pinout_scanner/             # JTAGulator-like (blueTag sobre pinout v2.x)
│   ├── daplink_usb/                # CMSIS-DAP v2 + HID v1
│   ├── buspirate_compat/           # OpenOCD via BusPirate binary (blueTag)
│   ├── flashrom_serprog/           # flashrom serprog (blueTag)
│   └── host_proto/
│       ├── emfi_proto/             # binario sobre CDC emfi
│       ├── crowbar_proto/          # binario sobre CDC crowbar
│       └── campaign_proto/         # streaming de resultados
│
├── drivers/                        # HW del FaultyCat v2.x
│   ├── ui_leds/
│   ├── ui_buttons/
│   ├── hv_charger/                 # flyback ~250V (faultycat origin)
│   ├── emfi_pulse/                 # PIO pulse gen (faultycat origin)
│   ├── crowbar_mosfet/             # N-MOSFET voltage crowbar (ya en v2.1)
│   ├── voltage_mux/                # si existe en v2.x (verificar F0)
│   ├── ext_trigger/                # trigger in (pines dedicados v2.1)
│   ├── target_monitor/             # ADC del target (v2.1)
│   └── scanner_io/                 # GPIOs dedicados scanner (v2.1 pinout)
│
├── hal/                            # thin wrapper sobre pico-sdk
│   ├── include/hal/
│   └── src/rp2040/
│
├── usb/                            # composite descriptor (carpeta propia)
│   ├── include/usb_descriptors.h
│   ├── src/usb_descriptors.c
│   └── src/usb_composite.c
│
├── third_party/                    # VENDORED, commits congelados
│   ├── pico-sdk/
│   ├── cmsis-dap/
│   ├── debugprobe/
│   ├── free-dap/
│   ├── blueTag/
│   └── faultier/
│
├── host/
│   └── faultycmd-rs/               # Rust workspace
│       ├── Cargo.toml
│       ├── crates/
│       │   ├── faultycmd-core/
│       │   ├── faultycmd-emfi/
│       │   ├── faultycmd-crowbar/
│       │   ├── faultycmd-scanner/
│       │   ├── faultycmd-dap/
│       │   ├── faultycmd-cli/
│       │   └── faultycmd-tui/
│       └── tests/
│
├── tests/
│   ├── glitch_engine_test.c
│   ├── swd_protocol_test.c
│   └── CMakeLists.txt
│
├── hardware/                       # KiCad v2.x ORIGINAL se conserva intacto
│   └── v2.x/                       # (copiado del repo main, solo referencia)
│
├── docs/
│   ├── ARCHITECTURE.md
│   ├── HARDWARE_V2.md              # mapeo del pinout v2.x (F0 obligatorio)
│   ├── USB_COMPOSITE.md
│   ├── PROTOCOLS.md
│   ├── SWD_INTERNALS.md
│   ├── JTAG_INTERNALS.md
│   ├── SAFETY.md                   # review HV, firmada
│   └── PORTING.md
│
├── tools/
│   ├── bootstrap.sh
│   └── flash.sh
│
├── .github/workflows/
│   ├── firmware.yml
│   └── faultycmd.yml
│
├── CMakeLists.txt
├── CMakePresets.json
└── README.md
```

### Principios de capas

1. **HAL** abstrae RP2040 sobre pico-sdk. No conoce nada de FaultyCat.
2. **drivers/** conoce pinout/periféricos del board v2.x. No conoce política.
3. **services/** orquesta. Ensambla drivers para funciones coherentes.
4. **usb/** configura el composite — transversal, consumido por varios services.
5. **apps/** ensambla todo en un binario.

---

## 4. USB Composite — descriptor definitivo

**Device class:** Miscellaneous Device (IAD-based composite)
**VID:PID:** reservar uno nuevo (NO reutilizar `2e8a:000c` de debugprobe)

```
Configuration Descriptor
├── IAD + CDC 0   "EMFI Control"        → IF 0 (notif) + IF 1 (data)
├── IAD + CDC 1   "Crowbar Control"     → IF 2 (notif) + IF 3 (data)
├── IAD + CDC 2   "Scanner Shell"       → IF 4 (notif) + IF 5 (data)
├── IAD + CDC 3   "Target UART"         → IF 6 (notif) + IF 7 (data)
├── Vendor IF     "CMSIS-DAP v2"        → IF 8 (2 bulk endpoints)
└── HID IF        "CMSIS-DAP v1"        → IF 9 (1 interrupt endpoint)
```

**Endpoints usados** (RP2040 hard-limit: 16 endpoints incluyendo IN/OUT):

- 4× CDC = 12 endpoints
- Vendor CMSIS-DAP v2 = 2 endpoints
- HID CMSIS-DAP v1 = 1 endpoint
- EP0 control = 1 endpoint
- **Total: 16** — justo en el límite

**Plan B** si no entra: eliminar HID v1 (la mayoría de hosts modernos soportan v2).
**Plan C:** fusionar Target UART dentro de CDC scanner.

**TinyUSB config (`tusb_config.h`):**

```c
#define CFG_TUD_CDC              4
#define CFG_TUD_VENDOR           1
#define CFG_TUD_HID              1
#define CFG_TUD_CDC_RX_BUFSIZE   256
#define CFG_TUD_CDC_TX_BUFSIZE   256
#define CFG_TUD_VENDOR_RX_BUFSIZE 1024
#define CFG_TUD_VENDOR_TX_BUFSIZE 1024
```

**Ownership de cada CDC:**

| CDC | Service responsable | Protocolo | Cliente típico |
|-----|--------------------|-----------|----------------|
| emfi | `services/glitch_engine/emfi/` | binario (`emfi_proto`) | `faultycmd emfi …` |
| crowbar | `services/glitch_engine/crowbar/` | binario (`crowbar_proto`) | `faultycmd crowbar …` |
| scanner | `services/pinout_scanner/` + `buspirate_compat/` + `flashrom_serprog/` | texto (shell blueTag) + modos binarios | Terminal, `openocd -f interface/buspirate.cfg`, `flashrom -p serprog:...` |
| target-uart | pass-through vía PIO UART | raw | `minicom` |
| cmsis-dap v2 | `services/daplink_usb/` | CMSIS-DAP v2 | OpenOCD, pyOCD, probe-rs |
| cmsis-dap v1 | `services/daplink_usb/` | CMSIS-DAP v1 HID | hosts antiguos |

### Mutex de bus SWD

Tres consumidores pueden querer SWD simultáneamente: `daplink_usb` (host externo), `glitch_engine/*` (verificación post-glitch), `pinout_scanner` (durante scan).

Política propuesta (detallada en F9):
1. **Prioridad estática**: campaña activa > scanner > daplink host externo.
2. **Exclusión mutua** con `mutex_t` del pico-sdk + timeout explícito.
3. Si `daplink_usb` recibe comando con bus tomado → responde `DAP_ERROR` / status=busy. Host reintenta.

---

## 5. Vendoring

### Submódulos

```
# .gitmodules
[submodule "third_party/pico-sdk"]
    path = third_party/pico-sdk
    url  = https://github.com/raspberrypi/pico-sdk.git
[submodule "third_party/debugprobe"]
    path = third_party/debugprobe
    url  = https://github.com/raspberrypi/debugprobe.git
[submodule "third_party/free-dap"]
    path = third_party/free-dap
    url  = https://github.com/ataradov/free-dap.git
[submodule "third_party/blueTag"]
    path = third_party/blueTag
    url  = https://github.com/Aodrulez/blueTag.git
[submodule "third_party/faultier"]
    path = third_party/faultier
    url  = https://github.com/hextreeio/faultier.git
```

Todos pineados a un **tag/commit específico** en F0.

### CMSIS-DAP headers (copia, no submódulo)

Copiamos `DAP.h`, `DAP_config.h` template en `third_party/cmsis-dap/` con `LICENSE` de ARM junto.

### `tools/bootstrap.sh`

```bash
#!/usr/bin/env bash
set -euo pipefail
git submodule update --init --recursive
(cd third_party/pico-sdk && git submodule update --init --recursive)
echo "✓ third_party populated"
```

### LICENSES directory

`LICENSES/` con copia de cada licencia upstream + `FaultyCat-LICENSE` del proyecto.

---

## 6. Plan por fases (Superpowers)

**Regla maestra:** cada fase termina con commit anotado y tag `v3.0-fN`. Claude Code **para** y espera validación humana antes de la siguiente.

### F0 — Bootstrap + vendoring + mapeo del HW v2.x + CI mínimo

**Entregables:**
- Árbol de directorios completo (carpetas vacías con `.gitkeep`).
- `.gitmodules` con los 5 submódulos pineados a versiones concretas (Claude Code propone, tú apruebas).
- `third_party/cmsis-dap/` con headers copiados.
- `CMakeLists.txt` raíz + `CMakePresets.json` (presets `fw-debug`, `fw-release`, `host-tests`).
- `tools/bootstrap.sh` funcional.
- `apps/faultycat_fw/main.c` = blink del LED (validación del pipeline).
- GitHub Action que compila UF2 en cada push.
- `docs/ARCHITECTURE.md` con diagrama de bloques.
- **`docs/HARDWARE_V2.md`** con mapeo EXHAUSTIVO del pinout v2.x leyendo el KiCad original:
  - qué GPIO del RP2040 va a cada función (HV, pulse, crowbar MOSFET, trigger in, trigger out, target ADC, scanner channels, botones, LEDs)
  - número exacto de canales del scanner JTAG/SWD
  - confirmación de qué tiene v2.1 vs v2.2
  - notas de safety sobre los pines HV
- Análisis breve del `firmware/c` actual documentado en `docs/PORTING.md` — qué se porta, qué se reemplaza.
- `LICENSES/` poblado.

**Criterio:**
- Clone limpio + bootstrap + cmake + build → produce UF2 que blinkea al flashear.
- `HARDWARE_V2.md` completo y revisado.

**Checkpoint humano:** flasheas UF2 al FaultyCat físico, confirmas blink. Revisas `HARDWARE_V2.md` contra el board real.

---

### F1 — HAL

**Entregables:**
- `hal/include/hal/{gpio,pio,dma,time,usb,adc,pwm}.h`.
- `hal/src/rp2040/*.c`.
- Tests unitarios host-side con fakes (`tests/hal_fake/`).

**Criterio:** blink de F0 reescrito sobre HAL, sigue funcionando. Tests pasan.

---

### F2 — Drivers HW (según pinout documentado en F0)

**Orden estricto (de bajo riesgo a alto):**
1. `ui_leds` + `ui_buttons`
2. `target_monitor` (ADC)
3. `scanner_io` (GPIOs dedicados del v2.x — N exactos según F0)
4. `ext_trigger` (pines dedicados del v2.1)
5. `crowbar_mosfet` (N-MOSFET ya montado en v2.1)
6. `voltage_mux` si existe en v2.x (si no, driver se queda como stub)
7. `hv_charger` (flyback ~250V, **safety-first**)
8. `emfi_pulse` (PIO pulse)

Cada driver expone comando `diag <driver>` por UART serial (temporal, antes de USB) para testing aislado.

**Criterio:** cada `diag <X>` funciona y se validó físicamente. Review de safety del `hv_charger` firmada en `docs/SAFETY.md`.

**Checkpoint humano:** probar cada driver, medir con osciloscopio los críticos (HV, pulse EMFI, pulse crowbar).

---

### F3 — USB composite (4 CDC + vendor + HID)

**Por qué es fase propia:** el descriptor con 10 interfaces es donde más falla el scaffold.

**Entregables:**
- `usb/src/usb_descriptors.c` con composite completo.
- `usb/src/usb_composite.c` con callbacks TinyUSB.
- Cada CDC en modo echo.
- Vendor IF responde a `DAP_Info` básico.
- HID IF stub.
- `docs/USB_COMPOSITE.md` con descriptor completo anotado.

**Criterio:**
- Linux: `lsusb -v` muestra 10 interfaces y los 4 CDCs aparecen como `/dev/ttyACM{0,1,2,3}`.
- macOS: `/dev/cu.usbmodem*` × 4.
- Windows: 4 COM ports.
- Echo funciona en los 4.
- `openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk; init; shutdown"` identifica el probe.

**Checkpoint humano:** 3 OSs si es posible, mínimo Linux.

---

### F4 — Glitch engine EMFI (port refactorizado del firmware v2.x)

**Entregables:**
- `services/glitch_engine/emfi/` — API `emfi_configure`, `emfi_arm`, `emfi_fire`, `emfi_status`.
- Usa `drivers/hv_charger` + `drivers/emfi_pulse` + `drivers/ext_trigger`.
- `services/host_proto/emfi_proto/` — protocolo binario sobre CDC emfi.
- Integrado en `main.c`, disparable desde host.

**Criterio:** paridad funcional con FaultyCat v2.2 en EMFI. Pulso verificable con osciloscopio.

---

### F5 — Glitch engine crowbar (faultier arch + faultycat v2.1 pinout)

**Entregables:**
- `services/glitch_engine/crowbar/` — API inspirada en faultier: `crowbar_configure(trigger_source, trigger_type, glitch_output)`, `crowbar_glitch(delay, pulse_width)`.
- Usa `drivers/crowbar_mosfet` + `drivers/voltage_mux` (si aplica) + `drivers/ext_trigger`.
- `services/host_proto/crowbar_proto/` sobre CDC crowbar.
- Test target: reproducir el tutorial de faultier (nRF52 APPROTECT bypass o equivalente).

**Criterio:** campaña simple de crowbar sobre un target conocido detecta diferencias de comportamiento. Paridad o superación del voltage glitching que ya trae el firmware v2.x actual.

---

### F6 — SWD core (debugprobe port)

**Entregables:**
- `services/swd_core/` portado desde `third_party/debugprobe/` en la nueva arquitectura.
- SWD phy via PIO, DP/AP/memory-AP/flash RP2040-specific.
- API interna: `swd_connect`, `swd_read32`, `swd_write32`, `swd_halt`, `swd_resume`, etc.
- Comando de diagnóstico por CDC scanner: `swd probe` lee DPIDR del target.

**Criterio:** `swd probe` conectado a target Pico retorna `0x0bc12477`. `swd read32 <addr>` funciona.

---

### F7 — CMSIS-DAP v2 + v1 (daplink_usb layer)

**Entregables:**
- `services/daplink_usb/cmsis_dap_v2/` — parser CMSIS-DAP sobre vendor IF bulk.
- `services/daplink_usb/cmsis_dap_v1_hid/` — sobre HID IF.
- Tabla de comandos soportados en `docs/PROTOCOLS.md`.

**Criterio:**
- `openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "program blink.elf verify reset exit"` funciona.
- `probe-rs list` muestra FaultyCat.
- `pyocd list` muestra FaultyCat.

**Checkpoint humano:** flashear un target con el probe integrado del FaultyCat.

---

### F8 — JTAG core + pinout scanner + BusPirate + serprog (blueTag port) ✓ closed `v3.0-f8` (2026-04-28)

**Entregables:**
- `services/jtag_core/` — JTAG state machine + bitbang via PIO (blueTag-derived).
- `services/pinout_scanner/` — JTAGulator algorithm sobre `drivers/scanner_io` (N canales según HARDWARE_V2.md).
- `services/buspirate_compat/` — BusPirate binary protocol para OpenOCD JTAG via FaultyCat.
- `services/flashrom_serprog/` — flashrom serprog protocol.
- Shell interactivo sobre CDC scanner (menú texto estilo blueTag).
- Boot mode por GPIO dedicado (tipo blueTag).

**Nota sobre canales:** blueTag standalone escanea 16 canales. El v2.x puede tener menos — el algoritmo se adapta al N disponible. Esto queda documentado en `HARDWARE_V2.md` (F0).

**Criterio:**
- `openocd -f interface/buspirate.cfg -c "buspirate_port /dev/ttyACM2" -f target/stm32fX.cfg` flashea target JTAG.
- `flashrom -p serprog:dev=/dev/ttyACM2 -r dump.bin` dumpea flash SPI.
- Scanner detecta pinout JTAG de un target conocido.

**Cierre F8 (2026-04-28):**

- Sub-fases F8-1..F8-5 cerradas con tags incrementales en `rewrite/v3`. F8-6 polish lands antes del tag `v3.0-f8`:
  - 3-read consistency check en `pinout_scan_jtag` / `_swd` para rechazar matches falsos por ruido del bus (observado empíricamente con un RP2040 cableado al scanner header — `0x6B5AD5AD` pasaba `jtag_idcode_is_valid` aunque RP2040 no tiene JTAG).
  - Mode-switch trailing-byte fix en `pump_shell_cdc`: el `\n` de `\r\n` ya no se cuela en el parser binario después de `buspirate enter` / `serprog enter`.
  - `docs/JTAG_INTERNALS.md` documenta TAP / scanner / BusPirate / serprog wire stack + mutual-exclusion contract + smoke results.
- Smoke físico 2026-04-28 (v2.2 + RP2040 USB-only): 13/13 checks verde — JTAG init/chain/idcode (no target → ERR no_target), soft-lock SWD↔JTAG, scan jtag/swd NO_MATCH, BusPirate handshake exacto (5×BBIO1 + OCD1), serprog handshake (NOP/Q_*/SPIOP read floating), disconnect detection, F4/F5 regression, F3 BOOTSEL.
- **No verificado físicamente** (necesita target externo): JTAG IDCODE contra STM32/ESP32, OpenOCD sobre BusPirate end-to-end, flashrom contra 25-series real. `scan swd` inherits F6 TXS0108E HW gate.
- Tests host-side: 26 binarios / 347 cases / 100% verde.
- Próxima fase activa: F9 (campaign manager + mutex SWD formal) — F7 sigue diferido hasta que un board rev / fly-wire bypass desbloquee F6 físicamente.

---

### F9 — Integración: campaign manager + mutex SWD ✓ closed `v3.0-f9` (2026-04-28)

**Entregables:**
- Mutex de bus SWD entre `daplink_usb`, `glitch_engine/*` y `pinout_scanner` (pico-sdk `mutex_t`).
- Campaign manager: sweep de (delay, width, power) con verificación SWD post-glitch y streaming de resultados.
- `services/host_proto/campaign_proto/` — streaming binario de resultados.
- State machine del mutex documentado en `docs/ARCHITECTURE.md`.

**Criterio:** campaña real detecta glitches exitosos. Killer feature validado.

**Cierre F9 (2026-04-28):**

- Sub-fases F9-1..F9-5 cerradas con commits incrementales en `rewrite/v3`. F9-6 polish lands antes del tag `v3.0-f9`:
  - F9-1: `services/swd_bus_lock/` — wrapper sobre flag volátil + owner tag (no `mutex_t` directo: cooperative single-core, no IRQ-side acquires; mantiene host-tests linkeables sin pico-sdk). 4 owner tags `IDLE/CAMPAIGN/SCANNER/DAPLINK`. 13 host tests.
  - F9-2: `services/campaign_manager/` — 6-state machine + cartesian sweep generator + 256-entry × 28 B ringbuffer + pluggable step executor (default no-op). 27 host tests. **Plan §F9 D1 dijo 24 B per record; las cuentas reales dan 28 B con `reserved[2]` para futuras flags — record size frozen at 28.**
  - F9-3: engine adapters en `apps/faultycat_fw/main.c` — blocking-with-cooperative-yield, levantan `swd_bus_try_acquire(CAMPAIGN)` alrededor del verify hook. Hook ships como **no-op stub** hasta que F6 desbloquee SWD físicamente; cuando lo haga, una sola línea conecta `swd_dp_read32` real al hook (no re-tag F9). Shell `campaign <subcmd>` agregado para smoke en CDC2.
  - F9-4: `services/host_proto/campaign_proto/` — opcodes `CAMPAIGN_CONFIG/START/STOP/STATUS/DRAIN` (0x20..0x24) extending emfi_proto y crowbar_proto. CRC16-CCITT framing, engine implícito por CDC. 17 host tests. **Polish requerido**: `CROWBAR_PROTO_MAX_PAYLOAD` era 64 B (pre-F9), bumped a 512 — DRAIN replies de hasta 505 B caían silenciosamente al guard `if (len > MAX) return 0`. También `pump_emfi/crowbar_cdc` reply[768] ahora `static` (defensivo vs stack overflow en deep executor wait loops).
  - F9-5: `tools/campaign_client.py` pyserial CLI mirror de emfi/crowbar_client.py. Subcommands `ping/configure/start/stop/status/drain/watch`. 30 ms gap intencional entre STATUS y DRAIN en watch loop para esquivar una race en el orden de dispatch durante el executor wait — F-future async refactor lo elimina.
  - F9-6 (este commit): `docs/MUTEX_INTERNALS.md` documenta el wire stack F9 completo (mutex semantics + state machine + sweep + protocol opcodes + smoke results).

- Smoke físico 2026-04-28 (v2.2): `campaign demo crowbar` shell + `campaign_client.py configure → start → watch` ambos completan sweeps end-to-end (6 steps en ~360 ms, results streaming exacto, mutex acquire/release sin deadlock). F4/F5 ping regression + F8 jtag/scan/buspirate handshakes + F3 BOOTSEL todos verde.

- **No verificado físicamente**: SWD verify hook real (gated by F6 HW unblock — TXS0108E) y la "killer feature" de detectar glitches exitosos contra un target real (gated by F6 + an actual target wired through the scanner header). El criterio de "campaña real detecta glitches exitosos" queda **partially validated**: el sweep + result streaming infrastructure está físicamente verde, lo que falta es la SWD verify del target post-fire.

- Tests host-side: 29 binarios / 404 cases / 100% verde.
- Próxima fase activa: F10 — faultycmd en Rust (workspace multi-crate).

---

### F10 — faultycmd en Python (Textual TUI + Rich CLI) ✓ closed `v3.0-f10` (2026-04-29)

> **Override 2026-04-28** de la decisión §1 #6. El equipo (Sabas + Electronic Cats) ya tiene reps con Textual + Rich en otros proyectos; reusar esa experiencia + reusar los 4 reference clients Python existentes (`tools/{emfi,crowbar,campaign}_client.py`, `tools/{swd,jtag,scanner}_diag.py`) baja el riesgo de framework choice a casi cero. El stack Rust (probe-rs + ratatui + clap) era técnicamente sólido pero implicaba contributor-onboarding más alto + cross-compile pain para distribución Win/Mac. Wire protocols (host_proto/* opcodes, frame format, mutex contract) **no cambian** — solo cambia el host language. Memoria `project_faultycmd_python_override.md` documenta el override formal para auditoría futura.

**Entregables:**
- `host/faultycmd-py/` package monorepo (single `pyproject.toml`).
- `faultycmd.framing` — CRC16-CCITT helper + frame builder (shared by todos los protocol modules).
- `faultycmd.usb` — port → CDC mapping helper (envuelve el snippet `udevadm` actual).
- `faultycmd.protocols.{emfi, crowbar, campaign, scanner, dap}` — clients consolidados (port directo de los 4 reference Python clients, no rewrite).
- `faultycmd.cli` — top-level CLI con `click` (command groups: `emfi`, `crowbar`, `campaign`, `scanner`, `tui`). Rich-rendered output (tablas, progress bars, status panels coloreados).
- `faultycmd.tui` — Textual app. Paneles HV / trigger / SWD-JTAG / campaign log. Hotkeys `E/C/S/D` para mode switch.
- Test suite: pytest + pytest-mock + Textual `Pilot` (snapshot tests para widgets).
- PyPI publish (TestPyPI primero, luego production como `faultycmd-tools` o nombre similar).
- Opcional: `pyinstaller` smoke binary para v3.0.0 release.
- CI workflow `host-py.yml` paralelo a `firmware.yml` (lint + tests + opcional PyInstaller cross-compile).

**Stack:**
- `pyproject.toml` estándar — operator elige su tooling (uv / poetry / hatch / plain pip).
- Python 3.10+ baseline.
- USB enum: `pyserial` + `udevadm` helper (Linux primary; Windows/macOS = TODO de F11 polish).

**Criterio:** TUI interactiva cubre 100% del faultycmd viejo + campañas + switch entre EMFI/crowbar/scanner. CLI cubre 100% de los 4 reference Python clients (estos quedan como deprecated reference hasta F11 archive).

**Cierre F10 (2026-04-29):**

- Sub-fases F10-1..F10-7 cerradas con commits incrementales en `rewrite/v3`:
  - F10-1: `host/faultycmd-py/` skeleton (pyproject.toml hatchling backend, BSD-3-Clause, src layout) + `faultycmd.framing` (CRC16-CCITT init=0xFFFF poly=0x1021 + frame builder/parser, espejo byte-por-byte del firmware) + `faultycmd.usb` (port → CDC mapping vía `udevadm`, reemplaza el snippet inline ad-hoc de `tools/`). 22 host tests.
  - F10-2: `faultycmd.protocols.{_base, emfi, crowbar, campaign}` — consolidación tipada de los 3 clients binarios. `BinaryProtoClient` base con context-manager + serial_factory hook (lazy `import serial` para test surface mínima). 35 nuevos tests = 57 total.
  - F10-3: `faultycmd.protocols.scanner` — line-based text-shell wrapper con prefix demuxer (SHELL: / SWD: / JTAG: / SCAN: / BPIRATE: / SERPROG: / CAMPAIGN:). Consolida `tools/{swd,jtag,scanner}_diag.py`. 15 nuevos tests = 72 total.
  - F10-4: `faultycmd.cli` — top-level click-based CLI con Rich-rendered output (tablas, Live updates, progress). Command tree: `info`, `emfi {ping/status/configure/arm/fire/disarm/capture}`, `crowbar {ping/status/configure/arm/fire/disarm}`, `campaign {status/configure/start/stop/drain/watch}` con `--engine`, `scanner {swd-/jtag-/scan-}`. Console script `faultycmd` instalado al `pip install -e .`.
  - F10-5: `faultycmd.tui` — Textual app, 2×2 grid + footer hotkeys (`q/r/c/s`). Paneles: EMFI (CDC0 status), Crowbar (CDC1 status), Campaign (live table + last-10 results), Diag (CDC2 snapshot tail con regex parser). 3 daemon polling threads + `call_from_thread` para UI updates. `SharedSerial` permite que Crowbar + Campaign clients compartan una sola CDC1 serial.Serial. 13 nuevos tests = 85 total.
  - F10-6: ruff lint config + 3-version pytest matrix CI workflow (`.github/workflows/host-py.yml`) + `python -m faultycmd` entry point (`__main__.py`) + Linux PyInstaller smoke build artifact.
  - F10-7: docs(F10) live-update + tag.
  - **F10-polish** (descubierto en smoke interactivo de la TUI 2026-04-29): `FaultycmdTUI.__init__` asignaba `self._workers: list[threading.Thread] = []`, clobbering Textual's `App._workers` (el `WorkerManager` backing field detrás de `App.workers`). Cada Static panel hacía `self.workers.cancel_node(self)` en unmount → `'list' has no cancel_node'` → cascade rompía la teardown asyncio → daemon threads `call_from_thread` después del loop closed → `RuntimeError: Event loop is closed`. Crash en launch + en `q`. Fix: rename `self._workers` → `self._poll_threads` (5 sites) + helper `_post()` que envuelve `call_from_thread` en `try/except RuntimeError` (defensa contra el shutdown race; 8 daemon-side call sites ruteados) + regression test `test_app_does_not_shadow_textual_workers` (asserts `app.workers` es `WorkerManager` y expone `cancel_node`). Los tests panel-state existentes no atrapaban esto porque inspeccionaban `panel.fields`/`panel.tail` sin manejar el lifecycle mount→unmount.

- **Override formal de §1 #6** (Rust → Python) commit `0a34a22` el 2026-04-28. Memoria `project_faultycmd_python_override.md` mantiene la auditoría legible para futuro yo / contributors.

- Smoke físico 2026-04-29 (FaultyCat v2.2 + Python 3.12):
  - `faultycmd info` → tabla Rich con los 4 CDCs (IF 0/2/4/6 → emfi/crowbar/scanner/target).
  - `faultycmd emfi ping` → `PONG b'F4\x00\x00'`. `faultycmd emfi status` → tabla Rich con state=IDLE.
  - `faultycmd crowbar ping` → `PONG b'F5\x00\x00'`. `faultycmd crowbar status` → tabla Rich con state=FIRED + output=LP de campaign anterior.
  - `faultycmd campaign --engine crowbar configure --delay 1000:3000:1000 --width 200:300:100 --power 1 --settle-ms 50 → start → watch` → Live Rich table streams 6/6 results, ending con `state=DONE step=6/6 pushed=6 dropped=0`.
  - `faultycmd scanner swd-init/swd-connect/swd-deinit` y `jtag-init/jtag-chain/jtag-deinit` retornan correctamente (no-target → ERR no_target o devices=0).
  - `faultycmd tui` lanza limpio, los 4 paneles populan, los 12 items del checklist pasan: launch + diag CDC2 stream + EMFI/Crowbar/Campaign panels + multiplex SharedSerial CDC1 + hotkeys `s`/`c`/`r`/`q` (toggle demo, clear log, reconnect, quit limpio sin traceback). Stop mid-sweep validado con sweep largo de 50 steps disparado por CLI en otra terminal + `s` mid-flight desde la TUI.

- Tests host-side: 86 cases / 100% verde bajo `pytest` (85 F10-1..F10-6 + 1 F10-polish regression guard). ruff `check src tests` clean. CI workflow corre `lint + test (3.10/3.11/3.12) + build-binary` paralelo a `firmware.yml`.

- Reference clients legacy (`tools/{emfi,crowbar,campaign}_client.py` + `tools/{swd,jtag,scanner}_diag.py`) permanecen en el tree como debug fallback. F11-5 archive los retira.

- **Scope honesto de lo que entregó `v3.0-f10`**: TUI = monitor + 6-step crowbar LP demo locked vía hotkey `s`. NO es UI de control completa: arm/fire/disarm directos, configure de sweep custom, SWD/JTAG operations, target serial passthrough, reflash desde TUI, etc., todo eso vive solo en CLI. El §F10 entregables prometía "100% del faultycmd viejo" — lo entregado es un subset. F11-0 lifts esto a "100%" antes del docs/benchmarks/release polish.

- Próxima fase activa: F11 — arranca con F11-0 (TUI complete control surface, 11 sub-fases) seguido de F11-1..F11-7 (docs sweep + benchmarks + safety review v2 + CHANGELOG + migration + archive + release `v3.0.0`).

- Post `v3.0.0`: F12 — GUI Web local (v3.1.0) para audiencia más amplia. Stack frozen: FastAPI + Tailwind + Alpine.js + Chart.js, instalado como submódulo `faultycmd.gui` dentro del mismo package. Ver §F12.

---

### F11 — Hardening, docs, release v3.0.0

> **Scope corregido 2026-04-29** después del smoke interactivo de la TUI (post `v3.0-f10`). El §F10 entregables prometía "TUI cubre 100% del faultycmd viejo + campañas + switch entre EMFI/crowbar/scanner". La TUI shipped en `v3.0-f10` cubre **monitor + 6-step demo locked**, no es UI de control completa. F11 abre con F11-0 — TUI complete control surface — antes del docs/benchmarks/release polish original.

**Entregables F11-0** — TUI control surface (lifts F10 a "100% del faultycmd viejo"):

- F11-0a: EMFI control modal — configure/arm/fire/disarm + capture viewer Rich table + last-config persistence + HV confirm modal.
- F11-0b: Crowbar control modal — configure LP+HP/arm/fire/disarm + last-config persistence.
- F11-0c: Campaign control modal — full sweep params (delay/width/power triplets START:END:STEP + settle_ms + engine selector). Reemplaza el `s` toggle demo locked.
- F11-0d: Scanner control modal — SWD (init/freq/connect/r32/w32/reset/deinit) + JTAG (init/reset/trst/chain/idcode/deinit) + scan-jtag/swd con progress en modal + pin assignment form.
- F11-0e: Target UART panel CDC3 — passthrough mini-terminal (lectura live + input box). Re-layout dashboard a 2×3 o tab para acomodar.
- F11-0f: Reflash action — magic baud 1200 BOOTSEL trigger desde TUI con file picker UF2 + wait reenum + reabrir CDCs (reemplaza `tools/flash.sh` desde dentro).
- F11-0g: Help modal (`?` hotkey) — hotkey reference + ownership map + safety reminders + version/git hash.
- F11-0h: CDC ownership + diag-mute indicator en footer/header.
- F11-0i: Lockfile concurrencia TUI ↔ CLI — `~/.cache/faultycmd/cdc-{0,2,4}.lock` con owner tag. CLI respeta + falla con mensaje claro; TUI también respeta lock CLI con `--force` escape hatch.
- F11-0j: Hardening — USB disconnect handling, smoke multi-terminal (alacritty/kitty/gnome-terminal/tmux), terminal resize sin crash.
- F11-0k: tests + docs(F11-0) + tag `v3.0-f11-0`.

**Entregables F11-1..F11-7** (release polish, original):

- F11-1: docs sweep — `docs/` completa: ARCHITECTURE, HARDWARE_V2, USB_COMPOSITE, PROTOCOLS, SWD_INTERNALS, JTAG_INTERNALS, MUTEX_INTERNALS, SAFETY, PORTING. README top-level apunta a `host/faultycmd-py/`.
- F11-2: benchmarks reproducibles — latencia trigger-a-pulso (EMFI + crowbar), throughput SWD (gated by F6 unblock), overhead `swd_bus_lock` mutex acquire/release. Scripts `tools/bench_*.py` + tabla en `docs/PERFORMANCE.md`.
- F11-3: HV safety review v2 — segunda pasada de `docs/SAFETY.md` con lecciones acumuladas F2b/F4/F5/F9 integradas. Firma final maintainer.
- F11-4: CHANGELOG completo + `docs/MIGRATION_FROM_V2.md` (VID/PID 1209:FA17 nuevo, magic baud 1200, frame format CRC16-CCITT, opcodes EMFI/CROWBAR/CAMPAIGN, shell scanner CDC2).
- F11-5: archive `tools/{emfi,crowbar,campaign}_client.py` + `tools/{swd,jtag,scanner}_diag.py` → `tools/legacy/` con README explicando que faultycmd los reemplaza.
- F11-6: GitHub release `v3.0.0` con UF2 firmware + tarball faultycmd-py + pyinstaller binary Linux. Notas de release derivadas del CHANGELOG. Tag `v3.0.0` sobre el commit del release.
- F11-7: docs(F11) + tag `v3.0-f11` (intermedio antes de `v3.0.0`).

**Criterio:** `v3.0.0` shippeable con TUI/CLI control completo + docs + benchmarks reproducibles + safety review firmada + migration path desde v2.x C legacy.

---

### F12 — GUI Web local (v3.1.0)

> **Decisión 2026-04-29:** después de cerrar `v3.0.0`, abrir track GUI para que la herramienta sea utilizable por audiencia más amplia (operadores técnicos + trainees + demos en conferencias + classroom), no solo CLI/TUI hackers. Decisión confirmada por Sabas.

**Stack frozen para F12:**

- **Backend**: FastAPI (Python ASGI) reusando 100% `faultycmd.protocols.*`, `faultycmd.framing`, `faultycmd.usb`. WebSocket para live updates (HV gauge, diag stream, campaign results streaming).
- **Frontend**: HTML + Tailwind CSS + Alpine.js (liviano, no build pipeline). Charts vía Chart.js / uPlot.
- **Empaquetado**: en el mismo `host/faultycmd-py/` package — submódulo `faultycmd.gui` con assets estáticos en `host/faultycmd-py/src/faultycmd/gui/static/`. Comando `faultycmd gui` arranca el server local + abre browser en `http://localhost:8080`. Single install, single release.
- **Audiencia**: mixed (operadores técnicos + trainees + conferencias + classroom).
- **NO entra a v3.0.0** — F12 = v3.1.0 release. v3.0.0 sale primero con TUI/CLI completo.

**Entregables F12 (v3.1.0):**

- F12-1: skeleton FastAPI server + Alpine.js frontend + WebSocket framing reuse + `faultycmd gui` console script.
- F12-2: Dashboard live — HV gauge bar (0-250V), trigger oscilloscope live, CDC ownership indicator, diag stream tail.
- F12-3: EMFI control panel — formulario configure + arm/fire/disarm con HV confirm + capture trace plot (Chart.js, NO Rich table como F11-0a; aquí va el plot real).
- F12-4: Crowbar control panel — formulario configure LP+HP + arm/fire/disarm.
- F12-5: Campaign sweep + heat-map — start/stop con live results streaming + heat-map fire/verify por delay×width usando uPlot/Chart.js.
- F12-6: Scanner / SWD/JTAG diagnose — pin assignment visual + connect + memory peek/poke + scan progress.
- F12-7: Target serial passthrough — terminal embebido vía xterm.js conectado al CDC3.
- F12-8: Reflash UI — file picker UF2 + drop into BOOTSEL + flash + reconnect.
- F12-9: tests (pytest backend + Playwright/Cypress smoke frontend) + docs + tag `v3.1.0`.

**Defer a F12+ patch releases (v3.1.x / v3.2):**

- Capture trace ADC ring plot real-time (post-F12-3 polish).
- A/B compare entre dos campaigns.
- Saved campaigns / sweep history (CSV/JSON export + import).
- Multi-language (i18n ES/EN).
- Replay step (re-run campaign step que produjo resultado interesante).
- Step-by-step single-shot debug mode.

**Criterio F12:** GUI web local cubre el 100% de los flujos del CLI/TUI v3.0.0 (sin BusPirate/serprog mode entries que se quedan CLI-only) + plots reales (no ASCII) + workflow accesible para no-CLI users + cross-platform sin packaging pain.

---

## 7. Prompt inicial para Claude Code

Pega esto como **primer mensaje** después de crear la rama `rewrite/v3`:

```
Vamos a hacer un rewrite total desde cero del firmware de FaultyCat
sobre el hardware v2.x EXISTENTE. No se diseña board nuevo.
Todo el plan, decisiones y arquitectura están en
FAULTYCAT_REFACTOR_PLAN.md en la raíz del repo — léelo ENTERO antes
de escribir una línea de código.

Resumen de decisiones irrevocables (no las re-litigues):
  - Rewrite total con arquitectura en capas (hal/drivers/services/apps).
  - Target HARDWARE: FaultyCat v2.x (v2.1/v2.2). NO hay board v3.
    El firmware se numera v3.0 pero el HW sigue siendo v2.x.
  - El HW v2.x YA TIENE todo lo necesario:
      * MOSFET crowbar (voltage glitching, nuevo en v2.1)
      * Trigger pins dedicados (nuevo en v2.1)
      * Analog input target monitor (nuevo en v2.1)
      * Pinout dedicado scanner JTAG/SWD (nuevo en v2.1)
      * HV charger flyback ~250V (desde v1)
      * EMFI pulse via PIO (desde v1)
  - Dependencias vendorizadas como submódulos pineados en third_party/:
    pico-sdk, debugprobe (primario CMSIS-DAP), free-dap (ref),
    blueTag (scanner), faultier (glitcher arch ref).
  - USB composite con 4 CDC + Vendor (CMSIS-DAP v2) + HID (v1):
      CDC emfi, CDC crowbar, CDC scanner, CDC target-uart.
  - Separación por tipo de ataque, NO por nivel:
      emfi = electromagnético (FaultyCat origin)
      crowbar = voltage glitching (faultier arch + pinout v2.1).
  - blueTag usa el pinout dedicado que ya existe en v2.x. El número
    exacto de canales se determina en F0 leyendo el KiCad original.
  - CMSIS-DAP: debugprobe primario, free-dap solo referencia
    (no compilar ambos).
  - faultycmd host = Rust workspace: crates por CDC + CLI + TUI ratatui.
  - Target MCU: RP2040. Romper backward compat del protocolo viejo.
  - Hardware real disponible (FaultyCat v2.x + Pico target).

Metodología (Superpowers):
  1. Trabajamos fase por fase F0→F11 del plan.
  2. Cada fase termina con commit + tag anotado (v3.0-fN).
  3. Al final de cada fase paras y reportas:
     (a) entregables,
     (b) checkpoints físicos que debo correr yo,
     (c) dudas abiertas.
     Esperas mi confirmación antes de F(N+1).
  4. No saltas fases. Si F(N) no está verde, no tocas F(N+1).
  5. Tests pasan siempre antes de commitear.
  6. Cualquier commit que toque hv_charger requiere checklist de
     safety firmado por mí en ese mismo commit.

Empieza con F0. Primero:
  1. Léeme el plan resumido estructurado.
  2. Confírmame que entendiste las 16 decisiones de la sección 1.
  3. Analiza el KiCad y el firmware C actual del repo original
     (ElectronicCats/faultycat rama main) y propón el contenido
     inicial de docs/HARDWARE_V2.md (mapeo de pinout) y
     docs/PORTING.md (qué se porta del firmware actual vs qué
     se reemplaza desde cero).
  4. Propón el plan DETALLADO de F0 sin escribir código:
     - commits/tags exactos de cada submódulo
     - contenido propuesto de CMakeLists.txt raíz y bootstrap.sh
     - estructura de CMakePresets.json
     - licencia del firmware (recomendado BSD-3 — justifica)
     - nombre de rama (sugerido: rewrite/v3)
  5. Yo apruebo o ajusto, y recién ahí ejecutas.

Preguntas que quiero que me respondas en tu propuesta de F0:
  a. ¿pico-sdk 2.1.x última o 2.0.0 estable? ¿Por qué?
  b. VID:PID del composite — pid.codes o rango dev.
  c. ¿Incluimos HID CMSIS-DAP v1 desde F3 o diferimos si hay
     presión de endpoints?
  d. ¿git submodule vs git subtree para los 5 repos?
  e. ¿Las refs (free-dap, blueTag, faultier) como submódulo
     completo o sparse-checkout?
  f. Licencia upstream de faultier (hextreeio) — verifica y
     dime si podemos portar código literal o solo usar la
     arquitectura como referencia.
  g. ¿Cuántos canales exactos tiene el scanner en v2.x según
     el KiCad?
```

---

## 8. Superpowers: skill del proyecto

Crea `.claude/skills/faultycat-fase-actual/SKILL.md` y actualízalo al inicio de cada fase. Ejemplo durante F3:

```markdown
---
name: faultycat-fase-actual
description: Contexto activo del rewrite FaultyCat v3 sobre HW v2.x. Consultar antes de cualquier commit, creación de archivo, o cambio de dirección.
---

# Fase actual: F3 — USB composite

## Qué está hecho
- v3.0-f0: bootstrap + vendoring + HARDWARE_V2.md + blink
- v3.0-f1: HAL con fakes
- v3.0-f2: 8 drivers con diag commands + safety review HV

## En qué estamos
Descriptor USB composite con 4 CDC + vendor + HID.
CDC emfi y CDC crowbar pasan echo. CDC scanner y target-uart pendientes.
Issue abierto: Windows no enumera bien el 4to CDC — investigar IAD order.

## Qué NO tocar
- services/glitch_engine/ (F4–F5)
- services/swd_core/ (F6)
- blueTag port (F8)

## Salida de F3
[x] lsusb -v muestra 10 interfaces
[x] ttyACM0..3 aparecen en Linux
[ ] COM0..3 aparecen en Windows
[ ] /dev/cu.usbmodem*×4 en macOS
[ ] openocd responde a DAP_Info
[ ] docs/USB_COMPOSITE.md completo

## Reglas extra de esta fase
- NO agregar servicios encima. Solo echo y stubs.
- Cualquier cambio al descriptor requiere diff documentado.
```

---

## 9. Riesgos y decisiones pendientes

### R1 — Endpoints USB al límite (alto)
RP2040 tiene 16 endpoints físicos; nuestra config usa los 16 exactos. Cualquier feature USB adicional requiere sacrificar algo. **Plan:** validar con un firmware mínimo de F3 antes de construir servicios. Plan B: eliminar HID v1.

### R2 — Mutex SWD (medio, decidir en F9)
3 consumidores potenciales. Política propuesta: prioridad estática campaña > scanner > daplink host externo, con timeout explícito.

### R3 — Scanner con menos de 16 canales (bajo)
Si el pinout v2.x ofrece menos de 16 pines dedicados para scanner, el JTAGulator tiene menos throughput que blueTag standalone. Mitigable: el algoritmo se adapta. Documentado en HARDWARE_V2.md (F0).

### R4 — Licencia del firmware (bajo, decidir en F0)
Recomendación: **BSD-3-Clause**. Alternativa: MIT. NO usar CC-BY-SA para código.

### R5 — `faultier` upstream licencia (verificar en F0)
Claude Code debe verificar la licencia del repo `hextreeio/faultier` antes de portar código. Si no es compatible, solo usar como referencia arquitectural (ideas, no código literal).

### R6 — VID:PID (bajo)
Sugerencia: solicitar PID a `pid.codes` (gratis, comunidad open source) bajo un VID existente (ej. `1209:XXXX`). Alternativa: rango dev `1234:5678` solo en desarrollo y migrar antes de release.

### R7 — Falta de rig HIL automático (bajo)
Los checkpoints físicos los corres tú. Aceptable. Post v3.0 se puede construir un rig con Pi + relés para CI HIL.

---

## 10. Relación con el firmware actual (v2.x)

El firmware actual en `ElectronicCats/faultycat/firmware/c` es la referencia crítica — ya implementó EMFI + voltage glitching + scanner sobre el HW v2.x. En F0 Claude Code debe leerlo y producir `docs/PORTING.md` con:

- **Qué se porta (adaptado a la nueva arquitectura):** rutinas HV charge, PIO pulse EMFI, trigger handling, scanner de pinout.
- **Qué se reemplaza (rewrite):** estructura del main, protocolo serial, bucle principal, drivers (todos los drivers se reescriben sobre HAL).
- **Qué se descarta:** código dead o ad-hoc que ya no aplica.

El firmware actual **NO se modifica** — se usa sólo como referencia. Los usuarios con board v2.x existente pueden:
- Seguir usando el firmware v2.x (el que viene de fábrica) sin cambios.
- Flashear el firmware v3.0 nuevo para obtener todas las features (EMFI + crowbar + SWD + scanner + CMSIS-DAP + multi-CDC).

---

## 11. Resumen de entregables de esta conversación

1. **Este documento** (`FAULTYCAT_REFACTOR_PLAN.md`) — commitear en la raíz del repo, actualizar por fase.
2. **Prompt inicial** (sección 7) — copia-pega en el primer mensaje a Claude Code.
3. **Template de skill** (sección 8) — crear `.claude/skills/faultycat-fase-actual/SKILL.md`.
4. **Árbol de directorios** (sección 3) — objetivo de F0.
5. **Descriptor USB** (sección 4) — spec de F3.
6. **Repos base y cómo se combinan** (sección 2) — reference card para cada fase.

## 12. Decisiones resueltas en F0 (2026-04-23)

Todas cerradas durante F0. Consolidadas aquí como referencia:

- ✓ **Rama**: `rewrite/v3`.
- ✓ **Licencia del firmware**: `BSD-3-Clause` (root `LICENSE`).
- ✓ **VID:PID dev (F0–F10)**: `1209:FA17` (pid.codes dev range).
  Antes de release v3.0.0 (F11): solicitar PID oficial a pid.codes.
- ✓ **HID CMSIS-DAP v1**: **incluir stub desde F3** para validar
  presupuesto de endpoints 16/16; parser real en F7.
- ✓ **Submódulos vs subtree**: `git submodule` para los cinco.
- ✓ **Refs en sparse-checkout?**: **No** — submódulo completo con
  shallow clone (`--depth=1`) vía `bootstrap.sh`.
- ✓ **Producción HW**: `v2.2` es el único que salió a producción; v2.1
  → v2.2 fue cambio de etiquetas únicamente.
- ✓ **Canales scanner en v2.x**: **8** (GP0–GP7) vía `Conn_01x10`
  (8 signal + VCC + GND). GP10 es LED STATUS, **no** es scanner.
- ✓ **Pinout completo**: documentado en `docs/HARDWARE_V2.md`.
- ✓ **Portable vs reescribible**: documentado en `docs/PORTING.md`.
- ✓ **Licencia `hextreeio/faultier`**: **NONE** (verificado 2026-04-23
  vía GitHub REST API — `license` field = `null`, no `LICENSE*` /
  `COPYING` en root). Consecuencia: `third_party/faultier/` es
  **solo referencia arquitectural**, con `EXCLUDE_FROM_ALL` en CMake.
  Ninguna línea se porta literal al v3. Ver
  `LICENSES/NOTICE-faultier.md`.
- ✓ **Commits/tags de submódulos pineados en F0**:
    - `third_party/pico-sdk`   → tag `2.1.1` (`bddd20f`)
    - `third_party/debugprobe` → tag `debugprobe-v2.3.0` (`780b827`)
    - `third_party/blueTag`    → tag `v2.1.2` (`887fc83`)
    - `third_party/free-dap`   → HEAD de `master` (`49a30aa`) al
      2026-04-23 (referencia cruzada, no compila)
    - `third_party/faultier`   → commit `1c78f3e` (referencia, no
      compila, sin licencia — ver NOTICE)
- ✓ **Corrección al §2**: `debugprobe` es **MIT** (no BSD-3 como
  figuraba originalmente).

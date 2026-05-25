# faultycmd — herramienta del host para FaultyCat v3

CLI y TUI en Python para operar el firmware FaultyCat v3 (rama
`rewrite/v3`, tags `v3.0-f0` en adelante). Sustituye a los scripts
sueltos pre-v3 y a los cuatro clientes de referencia que había en
`tools/`, unificándolos bajo un solo binario: `faultycmd`.

> **Nota de override (2026-04-28).** El plan §1 #6 originalmente
> exigía un workspace en Rust con TUI ratatui. Al cerrar F9 se cambió
> el stack del host a Python +
> [Textual](https://textual.textualize.io/) +
> [Rich](https://rich.readthedocs.io/). Los motivos —familiaridad del
> equipo, reúso directo de los clientes de referencia ya escritos en
> Python e iteración más rápida— están en `FAULTYCAT_REFACTOR_PLAN.md
> §F10` y en el commit del override. Los protocolos sobre el cable
> (wire protocols) **no cambian**.

## Estructura

```
faultycmd/
├── framing.py              CRC16-CCITT y armado/parseo de frames
├── usb.py                  mapeo puerto → CDC multiplataforma:
│                           pyserial list_ports en Linux/Windows/macOS,
│                           con udevadm como respaldo en Linux
├── persistence.py          almacén XDG del último config — un slot
│                           por motor (emfi / crowbar / campaign /
│                           scanner)
├── protocols/
│   ├── emfi.py             cliente de emfi_proto (CDC0, F4)
│   ├── crowbar.py          cliente de crowbar_proto (CDC1, F5)
│   ├── campaign.py         cliente de campaign_proto sobre CDC0/CDC1
│   │                       (F9-4)
│   ├── scanner.py          envoltorio del shell de texto en CDC2.
│   │                       Superficie pública de esta versión:
│   │                       `scan_swd`, `buspirate_enter`,
│   │                       `serprog_enter` y `parse_scan_swd_match`.
│   │                       Los verbos SWD de F6 y los JTAG de F8-1
│   │                       (`_swd_*` / `_jtag_*` / `_scan_jtag`)
│   │                       están WIP y se conservan como métodos con
│   │                       guion bajo para re-exponer en v3.1.
│   └── dap.py              envoltorio pyocd / cmsis-dap (stub hasta
│                           F7)
├── cli.py                  CLI con click y salida renderizada con Rich
├── tui.py                  dashboard Textual 2×2 (EMFI / Crowbar /
│                           Campaign / Diag-CDC2). Hotkeys: q r c s
│                           e b p n.
└── tui_modals.py           pantallas modales — una por motor:
                            • EmfiControlModal       (hotkey e)
                            • CrowbarControlModal    (hotkey b)
                            • CampaignControlModal   (hotkey p)
                            • ScannerControlModal    (hotkey n)
                            • HvConfirmModal         (confirma el
                                                      arm de EMFI)
```

## Inicio rápido

### 1. Crear y activar el venv

```bash
# Crear el entorno virtual dentro de host/faultycmd-py/
python -m venv venv

# Activar (Linux / macOS / Git Bash / WSL)
source venv/bin/activate

# Activar (Windows PowerShell)
# Si PowerShell rechaza el script por la Execution Policy,
# desbloquéalo SOLO para la sesión actual (no toca el resto
# del sistema):
#   Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
# Luego:
.\venv\Scripts\Activate.ps1

# Activar (Windows Command Prompt / CMD)
venv\Scripts\activate.bat
```

> **Execution Policy en Windows PowerShell.** Por seguridad, Windows
> bloquea por defecto los scripts de PowerShell no firmados. Como
> `Activate.ps1` no lleva firma, PowerShell lo rechaza con `running
> scripts is disabled on this system`. La solución, válida solo
> mientras esa terminal esté abierta:
>
> ```powershell
> Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
> ```
>
> `-Scope Process` limita el cambio a la sesión actual: al cerrar la
> terminal, la política restrictiva vuelve. No hace falta abrir
> PowerShell como administrador ni tocar la configuración global del
> sistema.

> **El venv depende de su ruta absoluta.** `python -m venv` graba la
> ruta del intérprete dentro de los scripts de activación y de
> `venv/pyvenv.cfg`. Si mueves o renombras la carpeta
> `host/faultycmd-py/` (o el propio `venv/`), el entorno deja de
> resolver y `faultycmd` no se encuentra. La solución es siempre la
> misma: borrar `venv/` y recrearlo con `python -m venv venv` en la
> nueva ubicación.

### 2. Instalar el paquete

```bash
# Modo editable + herramientas de dev (pytest, ruff)
pip install -e '.[dev]'
```

### 3. Usar la CLI

```bash
faultycmd --help
faultycmd emfi ping
faultycmd campaign configure --engine crowbar \
    --delay 1000:3000:1000 --width 200:300:100 --power 1
faultycmd campaign start
faultycmd campaign watch

# Scanner (shell sobre CDC2) — descubrimiento de pinout.
# En esta versión solo `scan-swd` es pública. Los verbos SWD directos
# (init/deinit/idcode/connect/read32/write32/freq) y los JTAG
# (init/deinit/chain/idcode + scan-jtag) siguen en WIP. `scan-swd`
# transmite tal cual las líneas SCAN: del firmware
# (MATCH/NO_MATCH/ERR) y termina; no inicia una sesión SWD después.
faultycmd scanner scan-swd
faultycmd scanner scan-swd --targetsel 01002927 --timeout-s 60
```

### 4. Lanzar la TUI

```bash
faultycmd tui
```

### Hotkeys de la TUI

Las letras que abren los modales (`e`, `b`, `p`, `n`) son mnemónicas
sobre el nombre del motor: **E**MFI, crow**B**ar, cam**P**aign,
sca**N** SWD.

| Tecla | Acción |
|-------|--------|
| `q` | salir (si hay un modal abierto, lo cierra primero) |
| `r` | reconectar (cierra y reabre las 4 CDC — útil tras reflashear) |
| `c` | limpiar el log en vivo de campaign |
| `s` | detener el sweep en curso (sin abrir modal) |
| `e` | modal EMFI (configure / arm / fire / disarm / capture) |
| `b` | modal Crowbar (configure / arm / fire / disarm) |
| `p` | modal Campaign (parámetros del sweep + start / stop / drain) |
| `n` | modal Scan SWD (un solo botón que dispara `scan swd` por CDC2) |

### Modal Scan SWD (`n`)

Modal con un único botón que ejecuta `scan swd` por el shell de texto
de CDC2 (P(8,2)=56 permutaciones, timeout de 30 s). Mientras corre el
scan, el flujo de diagnóstico de CDC2 se pausa para no contaminar la
salida, y se reanuda al terminar. Las líneas crudas del firmware
(`MATCH` / `NO_MATCH` / `ERR`) aparecen en la línea de estado del
modal.

En esta versión, un `MATCH` no abre un prompt para iniciar SWD a
continuación: el verbo `swd init` directo sigue en WIP. Por el mismo
motivo, las páginas manuales de init / deinit / freq / idcode /
connect / read32 / write32 / reset y todas las páginas JTAG están
retiradas del menú.

### Plataformas soportadas

| Sistema       | Estado | Notas |
|---------------|--------|-------|
| Linux         | ✓ verificado | Puertos en `/dev/ttyACM*`. Si aparece `Permission denied` al abrirlos, añade tu usuario al grupo `dialout` (`sudo usermod -aG dialout $USER`) y cierra sesión / vuelve a entrar. |
| Windows 10/11 | ✓ verificado (2026-05-25) | Puertos `COM*` enumerados por `usbser.sys` (driver inbox). Requiere firmware `v3.0-f11-0d` o posterior — versiones anteriores no llegaban a enumerar por bugs en el descriptor y en el orden de init. |
| macOS         | ⚠ no validado | La lógica multiplataforma (parsing de pyserial) debería bastar, pero no hay hardware para confirmarlo. |

### Siguientes sesiones

```bash
cd /ruta/a/host/faultycmd-py
# Activa el venv según tu consola (paso 1 de Inicio rápido)
faultycmd tui
```

Para salir del venv: `deactivate`.

### Polaridad del trigger (EMFI / Crowbar)

Ambos motores ofrecen los mismos cinco modos de trigger sobre el wire:

| Trigger          | Wire id | Programa PIO (WAITs)       | Evento que dispara el glitch |
|------------------|---------|----------------------------|------------------------------|
| `immediate`      | 0       | (ninguno)                  | arranca al instante con `fire` |
| `ext_rising`     | 1       | `WAIT 0, WAIT 1`           | flanco de subida |
| `ext_falling`    | 2       | `WAIT 1, WAIT 0`           | flanco de bajada |
| `ext_pulse_pos`  | 3       | `WAIT 0, WAIT 1, WAIT 0`   | flanco de bajada al final de un pulso LOW→HIGH→LOW |
| `ext_pulse_neg`  | 4       | `WAIT 1, WAIT 0, WAIT 1`   | flanco de subida al final de un pulso HIGH→LOW→HIGH |

Notas:

- El nivel idle de la línea de trigger se fija una sola vez al
  arrancar el firmware, en `main.c`
  (`ext_trigger_init(EXT_TRIGGER_PULL_DOWN)`): LOW-idle para todo el
  sistema. Los servicios no lo cambian en cada arm.
- En consecuencia, `ext_falling` y `ext_pulse_neg` requieren que la
  fuente externa lleve la línea a HIGH entre eventos. Sin esa
  estimulación activa, el pull-down interno y el level-shifter del
  board v2 mantienen la línea en LOW y el primer `WAIT 1` se queda
  esperando para siempre.
- La latencia desde el *evento de trigger* al pin de crowbar/EMFI es
  la misma en los cuatro modos de flanco (~56 ns). Si mides desde el
  *inicio* del pulso en vez de desde el flanco final que dispara el
  glitch, sumas el ancho del pulso a la lectura — eso es la posición
  del cursor en el osciloscopio, no overhead del firmware.
- `ext_pulse_pos` y `ext_pulse_neg` son inversos. Elige el que case
  con el nivel idle que genera tu fuente entre eventos; si no, el
  primer `WAIT` se cuelga.

### Timeout del trigger (EMFI / Crowbar fire)

Cada llamada a `fire` acepta un `trigger_timeout_ms` que acota cuánto
espera el firmware al trigger externo antes de cancelar la operación
con `*_ERR_TRIGGER_TIMEOUT`. Los valores por defecto y la semántica
son idénticos para EMFI y Crowbar:

- Por defecto: **60 000 ms** (1 minuto). Suficiente para triggers
  manuales sin tener que volver a la CLI a ajustar el valor.
- `0` significa **esperar indefinidamente**. El firmware lo aplica en
  `tick_waiting`; un `disarm` cancela la espera, libera la PIO y
  resetea el estado.

Dónde configurarlo:

- **TUI** (`e` / `b`): el formulario del modal expone un campo
  `trigger-timeout-ms`. Se lee en cada `fire`, así que se puede
  ajustar entre disparos sin volver a aplicar la configuración. El
  valor se persiste junto al resto del formulario en
  `last_config.json`.
- **CLI**: `faultycmd emfi fire --trigger-timeout-ms <ms>` y
  `faultycmd crowbar fire --trigger-timeout-ms <ms>`. Mismo valor por
  defecto (60 000).

## Estado

F10 cerrado en el tag `v3.0-f10` (2026-04-29). F11-0 —la superficie
completa de control en la TUI— está en desarrollo activo, con las
sub-fases F11-0a..k aterrizando una a una sobre `rewrite/v3`.
Progreso al checkpoint actual:

| Sub-fase | Tema | Estado |
|----------|------|--------|
| F11-0a | Modal EMFI con confirmación HV y autosave de capture | ✓ |
| F11-0b | Modal Crowbar y corrección del race en SharedSerial (CDC1) | ✓ |
| F11-0c | Modal Campaign (MVP con `engine=crowbar`) | ✓ |
| F11-0d | Modal Scan SWD (MVP de un solo botón; verbos JTAG / SWD directos siguen WIP) | ✓ MVP |
| F11-0e..k | Panel target UART, reflash, help, ownership, hardening, docs y tag | pendiente |

El roadmap autoritativo y el estado vigente viven en
[`FAULTYCAT_REFACTOR_PLAN.md`](../../FAULTYCAT_REFACTOR_PLAN.md)
(sección `§F11`) y en
`.claude/skills/faultycat-fase-actual/SKILL.md`.

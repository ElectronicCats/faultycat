# faultycmd — host tool for FaultyCat v3

Python CLI + TUI dashboard for the FaultyCat v3 firmware (`rewrite/v3`
branch, tags `v3.0-f0` through current). Replaces the pre-v3 standalone
scripts and the four reference clients under `tools/` with a single
`faultycmd` entry point.

> **Override note (2026-04-28)**: plan §1 #6 originally specified a
> Rust workspace + ratatui TUI. After F9 closed, the host-tool stack
> was switched to Python + [Textual](https://textual.textualize.io/) +
> [Rich](https://rich.readthedocs.io/) — see
> `FAULTYCAT_REFACTOR_PLAN.md §F10` and the override commit for
> rationale (team familiarity + reuse of existing Python reference
> clients + faster iteration). Wire protocols are unchanged.

## Layout

```
faultycmd/
├── framing.py              CRC16-CCITT + frame builder/parser
├── usb.py                  port → CDC mapping helper (cross-platform:
│                           pyserial list_ports on Linux/Windows/macOS,
│                           udevadm as Linux fallback)
├── persistence.py          XDG last-config store (one slot per engine:
│                           emfi / crowbar / campaign / scanner)
├── protocols/
│   ├── emfi.py             F4 emfi_proto client (CDC0)
│   ├── crowbar.py          F5 crowbar_proto client (CDC1)
│   ├── campaign.py         F9-4 campaign_proto over CDC0/CDC1
│   ├── scanner.py          text-shell wrapper over CDC2. Public
│   │                       surface in this release: `scan_swd`,
│   │                       `buspirate_enter`, `serprog_enter` +
│   │                       `parse_scan_swd_match`. F6 SWD and
│   │                       F8-1 JTAG verbs (`_swd_*` / `_jtag_*` /
│   │                       `_scan_jtag`) are WIP — kept as
│   │                       underscored methods for v3.1 re-expose.
│   └── dap.py              pyocd / cmsis-dap wrapper (stub until F7)
├── cli.py                  click-based CLI; Rich-rendered output
├── tui.py                  Textual 2×2 dashboard (EMFI / Crowbar /
│                           Campaign / Diag-CDC2). Hotkeys: q r c s
│                           e b p n.
└── tui_modals.py           Modal screens — one per engine:
                            • EmfiControlModal       (hotkey e)
                            • CrowbarControlModal    (hotkey b)
                            • CampaignControlModal   (hotkey p)
                            • ScannerControlModal    (hotkey n)
                            • HvConfirmModal         (gates EMFI arm)
```

## Quick start

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

> **Nota Windows — consolas y Execution Policy.** Dependiendo de la
> consola que uses, el comando de activación varía:
> - **PowerShell**: `.\venv\Scripts\Activate.ps1`
> - **CMD**: `venv\Scripts\activate.bat` (o solo `venv\Scripts\activate`)
> - **Git Bash / WSL / MSYS**: `source venv/bin/activate`
>
> Por defecto Windows bloquea scripts PowerShell no firmados (medida
> de seguridad contra scripts maliciosos). `Activate.ps1` no está
> firmado, así que PowerShell lo rechaza con `running scripts is
> disabled on this system`. La solución, válida solo para la sesión
> actual:
>
> ```powershell
> Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
> ```
>
> `-Scope Process` es clave: al cerrar la terminal, la política
> restrictiva vuelve. No requiere admin y no toca el sistema.

> **El venv queda atado a su ruta absoluta.** `python -m venv`
> graba el path absoluto del intérprete dentro de
> `venv/bin/activate` (Linux/macOS), `venv\Scripts\Activate.ps1`
> (Windows) y `venv/pyvenv.cfg`. Si mueves o renombras la carpeta
> `host/faultycmd-py/` (o el venv), el venv deja de funcionar y
> `faultycmd` deja de resolverse. Solución: borrar `venv/` y
> recrearlo con `python -m venv venv` en la nueva ubicación.

### 2. Instalar el paquete

```bash
# Editable + dev tools (pytest, ruff)
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

# Scanner (CDC2 shell) — pinout discovery
# Direct SWD verbs (init/deinit/idcode/connect/read32/write32/freq)
# and the JTAG verbs (init/deinit/chain/idcode + scan-jtag) are WIP
# and not exposed in this release. `scan-swd` just streams the
# firmware's SCAN: ... lines (MATCH/NO_MATCH/ERR) and exits — no
# follow-up init is offered.
faultycmd scanner scan-swd
faultycmd scanner scan-swd --targetsel 01002927 --timeout-s 60
```

### 4. Lanzar la TUI

```bash
faultycmd tui
```

### Plataformas soportadas

| Sistema | Estado | Notas |
|---|---|---|
| Linux  | ✓ verificado | `/dev/ttyACM*`. Si `Permission denied`, `sudo usermod -aG dialout $USER` + relogin. |
| Windows 10/11 | ✓ verificado (2026-05-25) | `COM*` enumerados vía `usbser.sys` (inbox). Requiere firmware `v3.0-f11-0d` o posterior — versiones previas no enumeraban por bugs de descriptor / init order. |
| macOS | ⚠ no validado | La lógica cross-platform (parser pyserial) debería funcionar pero falta hardware para confirmar. |

### Sesiones siguientes

```bash
cd /ruta/a/host/faultycmd-py
# Activar el venv (ver paso 1 según tu consola)
faultycmd tui
```

Para salir del venv: `deactivate`.

### TUI hotkeys

| Key | Action |
|-----|--------|
| `q` | quit (also closes the active modal first if one is open) |
| `r` | reconnect (drop + reopen the 4 CDCs — handy after re-flashing) |
| `c` | clear the campaign live log |
| `s` | stop the running sweep (express; no modal) |
| `e` | EMFI control modal (configure / arm / fire / disarm / capture) |
| `b` | crowBar control modal (configure / arm / fire / disarm) |
| `p` | camPaign control modal (full sweep params + start / stop / drain) |
| `n` | scan SWD modal (single `Scan SWD` action over CDC2 shell) |

### Scan SWD modal (`n`)

Single-button modal that runs `scan swd` over the CDC2 text shell
(P(8,2)=56 permutations, 30 s timeout). The diag tail on CDC2 is
paused for the duration of the call and reinstated automatically.
The raw scan output (MATCH / NO_MATCH / ERR lines) lands in the
modal's status line.

No follow-up auto-init prompt is offered after a MATCH — the
direct `swd init` verb is WIP-gated in this release. The manual
init / deinit / freq / idcode / connect / read32 / write32 /
reset action pages and the JTAG pages are also WIP and have been
pulled from the menu.

### Trigger polarity (EMFI / Crowbar)

Both engines expose the same five trigger options on the wire:

| Trigger          | Wire id | PIO program (WAITs)        | Last event (fires the glitch) |
|------------------|---------|----------------------------|-------------------------------|
| `immediate`      | 0       | (none)                     | starts immediately on `fire`  |
| `ext_rising`     | 1       | `WAIT 0, WAIT 1`           | rising edge                   |
| `ext_falling`    | 2       | `WAIT 1, WAIT 0`           | falling edge                  |
| `ext_pulse_pos`  | 3       | `WAIT 0, WAIT 1, WAIT 0`   | trailing falling edge of a LOW→HIGH→LOW pulse |
| `ext_pulse_neg`  | 4       | `WAIT 1, WAIT 0, WAIT 1`   | trailing rising edge of a HIGH→LOW→HIGH pulse |

Notes:

- The trigger line idle level is set once at boot by `main.c`
  (`ext_trigger_init(EXT_TRIGGER_PULL_DOWN)`), i.e. LOW-idle
  system-wide. The service layer does not change it per arm.
- This means `ext_falling` and `ext_pulse_neg` require the operator's
  physical trigger source to drive HIGH between events — the internal
  pull-down plus the v2 board's level-shifter will otherwise hold the
  line LOW and the leading `WAIT 1` stalls forever.
- Firmware latency from the *trigger event* to the crowbar/EMFI pin
  is the same in all four edge modes (~56 ns). If you measure from
  the *start* of a pulse instead of from the trailing edge that
  fires the glitch, you'll see the pulse width added on top — that
  is not firmware overhead, just where the oscilloscope cursor is.
- `ext_pulse_pos` and `ext_pulse_neg` are inverses. Pick the one
  whose idle level matches what your trigger source generates between
  events; otherwise the leading WAIT stalls.

### Trigger timeout (EMFI / Crowbar fire)

`fire` carries a per-call `trigger_timeout_ms` that bounds how
long the firmware waits for the external trigger before tripping
`*_ERR_TRIGGER_TIMEOUT`. Defaults and semantics are symmetric
across EMFI and Crowbar:

- Default: **60 000 ms** (1 minute) — enough for manual external
  triggers without having to reach for the CLI.
- `0` means **wait forever** (firmware enforces it in
  `tick_waiting` — disarm cancels the wait by tearing down the
  PIO and resetting state).

Where to set it:

- **TUI** (`e` / `b`): the modal form exposes a
  `trigger-timeout-ms` Input. It is read on every `fire` press,
  so the operator can tune it between fires without re-applying.
  The value is persisted alongside the rest of the form in
  `last_config.json`.
- **CLI**: `faultycmd emfi fire --trigger-timeout-ms <ms>` and
  `faultycmd crowbar fire --trigger-timeout-ms <ms>`. Same
  default (60 000).

## Status

F10 closed `v3.0-f10` (2026-04-29). F11-0 (TUI complete control
surface) is in active development — sub-fases F11-0a..k land
incrementally on `rewrite/v3`. Progress as of this checkpoint:

| Sub-fase | Subject | State |
|----------|---------|-------|
| F11-0a | EMFI control modal + HV confirm + capture autosave | ✓ |
| F11-0b | Crowbar control modal + SharedSerial CDC1 race fix | ✓ |
| F11-0c | Campaign control modal (engine=crowbar MVP) | ✓ |
| F11-0d | Scan SWD modal (single-button MVP; JTAG / direct-SWD verbs WIP-gated) | ✓ MVP |
| F11-0e..k | Target UART panel, reflash, help, ownership, hardening, docs+tag | pending |

See the project's main
[`FAULTYCAT_REFACTOR_PLAN.md`](../../FAULTYCAT_REFACTOR_PLAN.md)
`§F11` and `.claude/skills/faultycat-fase-actual/SKILL.md` for the
authoritative roadmap and current state.

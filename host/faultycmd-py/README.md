# faultycmd — host tool for FaultyCat v3

Python CLI and TUI for driving the FaultyCat v3 firmware (`rewrite/v3`
branch, tags `v3.0-f0` onwards). Replaces the loose pre-v3 scripts
and the four reference clients that used to live under `tools/`,
unifying them under a single binary: `faultycmd`.

> **Override note (2026-04-28).** Plan §1 #6 originally called for a
> Rust workspace with a ratatui TUI. When F9 closed, the host stack
> was switched to Python +
> [Textual](https://textual.textualize.io/) +
> [Rich](https://rich.readthedocs.io/). The reasons — team
> familiarity, direct reuse of the reference clients already written
> in Python, and faster iteration — are in
> `FAULTYCAT_REFACTOR_PLAN.md §F10` and in the override commit. The
> wire protocols **do not change**.

## Layout

```
faultycmd/
├── framing.py              CRC16-CCITT and frame build/parse
├── usb.py                  cross-platform port → CDC mapping:
│                           pyserial list_ports on Linux/Windows/macOS,
│                           with udevadm as fallback on Linux
├── persistence.py          XDG store for the last config — one slot
│                           per engine (emfi / crowbar / campaign /
│                           scanner)
├── protocols/
│   ├── emfi.py             EMFI protocol client
│   ├── crowbar.py          crowbar protocol client
│   ├── campaign.py         sweep protocol client
│   │                       (multiplexed over EMFI / crowbar)
│   ├── scanner.py          wrapper for the scanner text shell.
│   │                       Public surface in this release:
│   │                       `scan_swd`, `buspirate_enter`,
│   │                       `serprog_enter`, and `parse_scan_swd_match`.
│   │                       The direct SWD and JTAG verbs
│   │                       (`_swd_*` / `_jtag_*` / `_scan_jtag`)
│   │                       remain WIP and are kept as underscored
│   │                       methods so v3.1 can re-expose them.
│   └── dap.py              pyocd / cmsis-dap wrapper (stub)
├── cli.py                  click-based CLI with Rich-rendered output
├── tui.py                  2×2 Textual dashboard (EMFI / Crowbar /
│                           Campaign / Diag-CDC2). Hotkeys: q r c s
│                           e b p n.
└── tui_modals.py           modal screens — one per engine:
                            • EmfiControlModal       (hotkey e)
                            • CrowbarControlModal    (hotkey b)
                            • CampaignControlModal   (hotkey p)
                            • ScannerControlModal    (hotkey n)
                            • HvConfirmModal         (confirms the
                                                      EMFI arm)
```

## Install from a Release (recommended for end users)

If you only want to **use** `faultycmd` against a flashed FaultyCat —
not develop on it — grab the matching artifact from the
[GitHub Release](https://github.com/ElectronicCats/faultycat/releases)
that pairs with the UF2 you flashed:

| Your platform                       | Download                                          | Run with                                  |
|-------------------------------------|---------------------------------------------------|-------------------------------------------|
| Windows (no Python needed)          | `faultycmd_vX.Y.Z.W.exe`                          | `faultycmd_vX.Y.Z.W.exe info`             |
| Windows with Python / Linux / macOS | `faultycmd-X.Y.Z.W-py3-none-any.whl`              | `pip install <wheel>` then `faultycmd info` |
| Building from source                | `faultycmd-X.Y.Z.W.tar.gz`                        | `pip install <tarball>` then `faultycmd info` |

The host package validates firmware parity on every connect — see
the "Firmware ↔ host version parity" subsection below. If you flash
firmware `vX.Y.Z.W`, install host `vX.Y.Z.W`; the two are released
together for that reason.

The rest of this document covers the **developer** install (editable
checkout + venv) used while iterating on the host code itself.

## Quick start (developer install)

### 1. Create and activate the venv

```bash
# Create the virtual environment inside host/faultycmd-py/
python -m venv venv

# Activate (Linux / macOS / Git Bash / WSL)
source venv/bin/activate

# Activate (Windows PowerShell)
# If PowerShell rejects the script because of the Execution Policy,
# unblock it ONLY for the current session (does not touch the rest
# of the system):
#   Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
# Then:
.\venv\Scripts\Activate.ps1

# Activate (Windows Command Prompt / CMD)
venv\Scripts\activate.bat
```

> **Execution Policy on Windows PowerShell.** For safety, Windows
> blocks unsigned PowerShell scripts by default. Since `Activate.ps1`
> is unsigned, PowerShell rejects it with `running scripts is
> disabled on this system`. The fix, valid only while that terminal
> stays open:
>
> ```powershell
> Set-ExecutionPolicy -ExecutionPolicy Bypass -Scope Process
> ```
>
> `-Scope Process` limits the change to the current session: closing
> the terminal restores the restrictive policy. You do not need to
> open PowerShell as administrator or touch the system-wide policy.

> **The venv depends on its absolute path.** `python -m venv` bakes
> the interpreter path into the activation scripts and into
> `venv/pyvenv.cfg`. If you move or rename the `host/faultycmd-py/`
> folder (or the `venv/` folder itself), the environment stops
> resolving and `faultycmd` cannot be found. The fix is always the
> same: delete `venv/` and recreate it with `python -m venv venv` at
> the new location.

### 2. Install the package

```bash
# Editable install + dev tools (pytest, ruff)
pip install -e '.[dev]'
```

### 3. Use the CLI

> **Windows note.** If you only need to run `faultycmd` and don't
> want to deal with Python at all, download the standalone
> `faultycmd_vX.Y.Z.W.exe` from the
> [GitHub Release](https://github.com/ElectronicCats/faultycat/releases)
> matching your firmware tag and put it anywhere convenient. The
> `.exe` bundles Python + every dependency. Inside this Quick start
> you'd skip steps 1 and 2 entirely and just run
> `faultycmd_vX.Y.Z.W.exe info`.
>
> If you installed via `pip install --user` outside a venv and your
> shell does not recognise the `faultycmd` command, your Python
> user-install Scripts directory is probably not on `PATH`. Two
> workarounds:
>
>   - Activate a venv (step 1 of Quick start). Inside the venv,
>     `pip install` puts `faultycmd.exe` in `<venv>\Scripts\` which
>     is added to `PATH` on activation.
>   - Or use the module invocation which never depends on `PATH`:
>     `python -m faultycmd info`, `python -m faultycmd tui`, etc.
>
> Linux and macOS users get this for free — `pip install` adds the
> script to a directory that is already on `PATH` for any non-root
> install backed by a venv.

```bash
# Discover the connected board and inspect its state.
faultycmd --help
faultycmd info
faultycmd emfi ping
faultycmd emfi status

# Run a crowbar parameter sweep (crowbar is the default engine).
faultycmd campaign configure \
    --delay 1000:3000:1000 --width 200:300:100 --power 1
faultycmd campaign start
faultycmd campaign watch

# Scan the SWD pinout of the target.
faultycmd scanner scan-swd
```

`faultycmd --help` lists every command group available in this
release; `faultycmd <group> --help` shows the subcommands and
flags for each one.

### Firmware ↔ host version parity

`faultycmd` only talks to firmware that was built from the same
release tag. Every protocol client validates this on connect, so a
command like `faultycmd emfi status` against a mismatched board
exits with code `3` and a message naming both versions:

```
$ faultycmd emfi status
version mismatch firmware/host version mismatch:
  firmware=3.0.0.0, host=3.0.1.0. Re-flash the matching UF2 from the
  GitHub Release, or pass --ignore-version-mismatch to bypass
  (unsafe — wire protocol may have shifted).
```

`faultycmd info` is the diagnostic path that never aborts on a
mismatch: it lists the CDC interfaces, probes the EMFI CDC for the
firmware version, and prints a coloured `match` / `mismatch` line.
The TUI shows the same parity in its header subtitle
(`host v3.0.1.0  ·  fw v3.0.0.0 ✗ (host v3.0.1.0)`).

To bypass the check for a development build of the firmware:

```bash
faultycmd --ignore-version-mismatch tui
```

Use this only when iterating on a hand-built UF2 against a
hand-built host package; in field operation a mismatched pairing is
almost always a half-applied upgrade and produces silently-wrong
results if the wire protocol drifted. See
[`docs/RELEASES.md`](../../docs/RELEASES.md) for the full picture
(version scheme, where the version lives in the source tree, how
the firmware advertises it, how to cut a release).

### 4. Launch the TUI

```bash
faultycmd tui
```

### TUI hotkeys

The letters that open the modals (`e`, `b`, `p`, `n`) are mnemonics
on the engine name: **E**MFI, crow**B**ar, cam**P**aign,
sca**N** SWD.

| Key | Action |
|-----|--------|
| `q` | quit (if a modal is open, closes it first) |
| `r` | reconnect (closes and reopens the 4 CDCs — handy after re-plugging the board) |
| `c` | clear the campaign live log |
| `s` | stop the running sweep (without opening a modal) |
| `e` | EMFI modal (configure / arm / fire / disarm / capture) |
| `b` | Crowbar modal (configure / arm / fire / disarm) |
| `p` | Campaign modal (sweep parameters + start / stop / drain) |
| `n` | Scan SWD modal (single button that fires `scan swd` over CDC2) |

### Scan SWD modal (`n`)

A single-button modal that runs `scan swd` over the CDC2 text shell
(P(8,2)=56 permutations, 30 s timeout). While the scan is running the
CDC2 diagnostic stream is paused so it doesn't contaminate the output,
and it resumes when the scan finishes. The raw firmware lines
(`MATCH` / `NO_MATCH` / `ERR`) appear on the modal's status line.

### Supported platforms

| System        | Status | Notes |
|---------------|--------|-------|
| Linux         | ✓ verified | Ports under `/dev/ttyACM*`. If you hit `Permission denied` when opening them, add your user to the `dialout` group (`sudo usermod -aG dialout $USER`) and log out / back in. Install via wheel from a Release. |
| Windows 10/11 | ✓ verified (2026-05-25) | `COM*` ports enumerated by `usbser.sys` (inbox driver). Requires firmware `v3.0-f11-0d` or later — earlier versions failed to enumerate because of bugs in the descriptor and in the init order. **Easiest install path: download `faultycmd_vX.Y.Z.W.exe` from the Release — no Python install needed.** If you prefer pip, use a venv so `Scripts/` ends up on `PATH`; otherwise `python -m faultycmd ...` works without PATH changes. |
| macOS         | ⚠ not validated | The cross-platform logic (pyserial parsing) should be enough, but no hardware on hand to confirm. Install via wheel from a Release. |

### Subsequent sessions

```bash
cd /path/to/host/faultycmd-py
# Activate the venv for your shell (step 1 of Quick start)
faultycmd tui
```

To leave the venv: `deactivate`.

### Trigger polarity (EMFI / Crowbar)

Both engines expose the same five trigger modes on the wire:

| Trigger          | Wire id | PIO program (WAITs)        | Event that fires the glitch |
|------------------|---------|----------------------------|-----------------------------|
| `immediate`      | 0       | (none)                     | starts immediately on `fire` |
| `ext_rising`     | 1       | `WAIT 0, WAIT 1`           | rising edge |
| `ext_falling`    | 2       | `WAIT 1, WAIT 0`           | falling edge |
| `ext_pulse_pos`  | 3       | `WAIT 0, WAIT 1, WAIT 0`   | falling edge at the end of a LOW→HIGH→LOW pulse |
| `ext_pulse_neg`  | 4       | `WAIT 1, WAIT 0, WAIT 1`   | rising edge at the end of a HIGH→LOW→HIGH pulse |

Notes:

- The idle level of the trigger line is set once when the firmware
  boots, in `main.c`
  (`ext_trigger_init(EXT_TRIGGER_PULL_DOWN)`): LOW-idle for the whole
  system. Services do not change it on every arm.
- As a consequence, `ext_falling` and `ext_pulse_neg` require the
  external source to drive the line HIGH between events. Without
  that active stimulation, the internal pull-down and the v2 board's
  level-shifter keep the line LOW and the first `WAIT 1` waits
  forever.
- Latency from the *trigger event* to the crowbar/EMFI pin is the
  same in all four edge modes (~56 ns). If you measure from the
  *start* of the pulse instead of from the final edge that fires the
  glitch, you add the pulse width to the reading — that's the cursor
  position on your scope, not firmware overhead.
- `ext_pulse_pos` and `ext_pulse_neg` are inverses. Pick the one that
  matches the idle level your source produces between events;
  otherwise the first `WAIT` hangs.

### Trigger timeout (EMFI / Crowbar fire)

Every call to `fire` accepts a `trigger_timeout_ms` that bounds how
long the firmware waits for the external trigger before cancelling
with `*_ERR_TRIGGER_TIMEOUT`. The defaults and semantics are
identical for EMFI and Crowbar:

- Default: **60 000 ms** (1 minute). Enough for manual triggers
  without having to go back to the CLI to adjust the value.
- `0` means **wait forever**. The firmware honours it in
  `tick_waiting`; a `disarm` cancels the wait, releases the PIO, and
  resets the state.

Where to configure it:

- **TUI** (`e` / `b`): the modal form exposes a `trigger-timeout-ms`
  field. It is read on every `fire`, so it can be tweaked between
  shots without re-applying the configuration. The value is
  persisted alongside the rest of the form in `last_config.json`.
- **CLI**: `faultycmd emfi fire --trigger-timeout-ms <ms>` and
  `faultycmd crowbar fire --trigger-timeout-ms <ms>`. Same default
  (60 000).

## Status

F10 closed at tag `v3.0-f10` (2026-04-29). F11-0 — the full TUI
control surface — is in active development, with sub-phases
F11-0a..k landing one by one on `rewrite/v3`. Progress at the
current checkpoint:

| Sub-phase | Topic | Status |
|-----------|-------|--------|
| F11-0a | EMFI modal with HV confirm and capture autosave | ✓ |
| F11-0b | Crowbar modal and SharedSerial race fix (CDC1) | ✓ |
| F11-0c | Campaign modal (MVP with `engine=crowbar`) | ✓ |
| F11-0d | Scan SWD modal (single-button MVP; direct JTAG / SWD verbs still WIP) | ✓ MVP |
| F11-0e..k | Target UART panel, reflash, help, ownership, hardening, docs and tag | pending |

The authoritative roadmap and current status live in
[`FAULTYCAT_REFACTOR_PLAN.md`](../../FAULTYCAT_REFACTOR_PLAN.md)
(section `§F11`) and in
`.claude/skills/faultycat-fase-actual/SKILL.md`.

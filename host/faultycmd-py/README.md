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
├── usb.py                  port → CDC mapping helper (udevadm wrapped)
├── persistence.py          XDG last-config store (one slot per engine:
│                           emfi / crowbar / campaign / scanner)
├── protocols/
│   ├── emfi.py             F4 emfi_proto client (CDC0)
│   ├── crowbar.py          F5 crowbar_proto client (CDC1)
│   ├── campaign.py         F9-4 campaign_proto over CDC0/CDC1
│   ├── scanner.py          text-shell wrapper over CDC2 (F6 swd /
│   │                       F8-1 jtag / F8-2 scan / F8-4 buspirate
│   │                       / F8-5 serprog mode-switch).
│   │                       Also exports `parse_scan_swd_match` to
│   │                       extract SWCLK/SWDIO from a `scan swd`
│   │                       MATCH line.
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
                            • SwdInitFromScanModal   (post scan-swd
                              follow-up: pre-fills detected SWCLK /
                              SWDIO + asks for NRST, default GP0)
```

## Quick start

```bash
# Install (editable while developing)
pip install -e '.[dev]'

# Run the CLI
faultycmd --help
faultycmd emfi ping
faultycmd campaign configure --engine crowbar \
    --delay 1000:3000:1000 --width 200:300:100 --power 1
faultycmd campaign start
faultycmd campaign watch

# Scanner (CDC2 shell) — interactive: scan + offer to init
faultycmd scanner scan-swd                # asks Y/n + NRST after MATCH
faultycmd scanner scan-swd --init --nrst 0   # non-interactive (scripts)
faultycmd scanner scan-swd --no-init      # just scan, never init
faultycmd scanner swd-init 0 1 2          # manual init
faultycmd scanner swd-idcode

# Run the TUI
faultycmd tui
```

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
| `n` | scanNer / SWD control modal (see below) |

### Scanner / SWD modal (`n`)

Two-step wizard:

1. **Menu page** — 5×2 grid of action buttons:
   `Init` · `Deinit` · `Freq` · `IDCODE` · `Connect` ·
   `Read32` · `Write32` · `Reset` · `Scan SWD` · `Close`.
2. **Per-action page** — only the inputs that action needs (or a
   short "no parameters" notice), plus `Aceptar` (dispatches) and
   `Atrás` (back to menu). Results land in the modal's own status
   line; the diag tail on CDC2 is paused for the duration of the
   call and reinstated automatically.

Pin defaults mirror `drivers/include/board_v2.h`:
`SWCLK=GP0`, `SWDIO=GP1`, `NRST=GP0` (editable; blank = no NRST).
The last-applied values per engine persist under
`$XDG_CONFIG_HOME/faultycmd/last_config.json`.

After a successful `Scan SWD`, a follow-up `SwdInitFromScanModal`
pops up showing the detected SWCLK / SWDIO plus an editable NRST
field (default GP0). `Aceptar` runs `swd init` with those pins;
`Cerrar` skips the init.

## Status

F10 closed `v3.0-f10` (2026-04-29). F11-0 (TUI complete control
surface) is in active development — sub-fases F11-0a..k land
incrementally on `rewrite/v3`. Progress as of this checkpoint:

| Sub-fase | Subject | State |
|----------|---------|-------|
| F11-0a | EMFI control modal + HV confirm + capture autosave | ✓ |
| F11-0b | Crowbar control modal + SharedSerial CDC1 race fix | ✓ |
| F11-0c | Campaign control modal (engine=crowbar MVP) | ✓ |
| F11-0d | Scanner / SWD control modal + post-scan init prompt | ✓ MVP |
| F11-0e..k | Target UART panel, reflash, help, ownership, hardening, docs+tag | pending |

See the project's main
[`FAULTYCAT_REFACTOR_PLAN.md`](../../FAULTYCAT_REFACTOR_PLAN.md)
`§F11` and `.claude/skills/faultycat-fase-actual/SKILL.md` for the
authoritative roadmap and current state.

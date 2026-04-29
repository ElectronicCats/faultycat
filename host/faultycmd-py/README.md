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
├── protocols/
│   ├── emfi.py             F4 emfi_proto client (CDC0)
│   ├── crowbar.py          F5 crowbar_proto client (CDC1)
│   ├── campaign.py         F9-4 campaign_proto over CDC0/CDC1
│   ├── scanner.py          text-shell wrapper over CDC2 (F6 swd /
│                           F8-1 jtag / F8-2 scan / F8-4 buspirate
│                           / F8-5 serprog mode-switch)
│   └── dap.py              pyocd / cmsis-dap wrapper (stub until F7)
├── cli.py                  click-based CLI; Rich-rendered output
└── tui.py                  Textual app (HV / trigger / SWD / campaign
                            panels; E/C/S/D hotkeys for mode switch)
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

# Run the TUI
faultycmd tui
```

## Status

F10 is in active development — sub-phases F10-1..F10-7 land
incrementally on `rewrite/v3`. See the project's main
`FAULTYCAT_REFACTOR_PLAN.md §F10` and `.claude/skills/
faultycat-fase-actual/SKILL.md` for the current state.

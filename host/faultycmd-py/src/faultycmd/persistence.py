"""F11-0a — XDG-compliant per-engine last-config store.

The TUI control modals (EMFI / Crowbar / Campaign / Scanner) prefill
their forms from the last successful Apply config so the operator
doesn't retype the same delay/width 50 times in a session. The store
is one JSON file with a top-level dict keyed by engine name:

    {
      "emfi":     {"trigger": "ext_rising", "delay_us": 1200, ...},
      "crowbar":  {"trigger": "immediate", "output": "lp", ...},
      "campaign": {"engine": "crowbar", "delay": "1000:5000:100", ...},
      "scanner":  {"swclk_gp": 0, "swdio_gp": 1, ...}
    }

We deliberately skip a per-engine-file split — the store is small
(< 1 KB) and a single file makes atomic writes simple via
`os.replace` of a sibling temp file.

Path resolution follows the XDG Base Directory spec:
    $XDG_CONFIG_HOME/faultycmd/last_config.json
or, if unset:
    ~/.config/faultycmd/last_config.json

A corrupt or unreadable file is treated as empty (we don't want a
TUI launch to fail because the operator hand-edited the JSON wrong).
"""
from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path


class LastConfig:
    def __init__(self) -> None:
        xdg = os.environ.get("XDG_CONFIG_HOME")
        if xdg:
            base = Path(xdg)
        else:
            base = Path(os.environ.get("HOME", str(Path.home()))) / ".config"
        self.path = base / "faultycmd" / "last_config.json"

    def load(self, engine: str) -> dict:
        try:
            with self.path.open("r", encoding="utf-8") as f:
                data = json.load(f)
        except (FileNotFoundError, json.JSONDecodeError, OSError):
            return {}
        if not isinstance(data, dict):
            return {}
        engine_dict = data.get(engine, {})
        if not isinstance(engine_dict, dict):
            return {}
        return engine_dict

    def save(self, engine: str, params: dict) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        # Read-modify-write: load full doc, replace one engine slot.
        try:
            with self.path.open("r", encoding="utf-8") as f:
                doc = json.load(f)
            if not isinstance(doc, dict):
                doc = {}
        except (FileNotFoundError, json.JSONDecodeError, OSError):
            doc = {}
        doc[engine] = dict(params)
        # Atomic write: NamedTemporaryFile in same dir + os.replace.
        # delete=False because we'll move it; cleanup on success.
        fd, tmp_path = tempfile.mkstemp(
            prefix="last_config.json.",
            dir=str(self.path.parent),
        )
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                json.dump(doc, f, indent=2, sort_keys=True)
            os.replace(tmp_path, self.path)
        except Exception:
            try:
                os.unlink(tmp_path)
            except OSError:
                pass
            raise

"""F11-0a — last-config persistence (XDG).

`LastConfig` stores per-engine form params so the TUI control
modals can prefill from the last successful submit. The store is
JSON at `$XDG_CONFIG_HOME/faultycmd/last_config.json` (defaulting
to `~/.config/faultycmd/last_config.json`).
"""
from __future__ import annotations

import pytest

from faultycmd.persistence import LastConfig


@pytest.fixture
def tmp_xdg(tmp_path, monkeypatch):
    """Re-point XDG_CONFIG_HOME at a tmp dir and return the
    expected file path."""
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    return tmp_path / "faultycmd" / "last_config.json"


def test_load_returns_empty_dict_when_no_file(tmp_xdg):
    cfg = LastConfig()
    assert cfg.load("emfi") == {}
    assert cfg.load("crowbar") == {}


def test_save_then_load_roundtrip(tmp_xdg):
    cfg = LastConfig()
    cfg.save("emfi", {"trigger": "immediate", "delay_us": 0, "width_us": 5})
    loaded = cfg.load("emfi")
    assert loaded == {"trigger": "immediate", "delay_us": 0, "width_us": 5}


def test_save_creates_parent_directory(tmp_xdg):
    assert not tmp_xdg.parent.exists()
    LastConfig().save("emfi", {"x": 1})
    assert tmp_xdg.exists()
    assert tmp_xdg.parent.is_dir()


def test_save_one_engine_does_not_clobber_others(tmp_xdg):
    cfg = LastConfig()
    cfg.save("emfi", {"width_us": 5})
    cfg.save("crowbar", {"width_ns": 200})
    assert cfg.load("emfi") == {"width_us": 5}
    assert cfg.load("crowbar") == {"width_ns": 200}


def test_save_overwrites_same_engine(tmp_xdg):
    cfg = LastConfig()
    cfg.save("emfi", {"width_us": 5})
    cfg.save("emfi", {"width_us": 10})
    assert cfg.load("emfi") == {"width_us": 10}


def test_load_returns_empty_on_corrupt_json(tmp_xdg, capsys):
    tmp_xdg.parent.mkdir(parents=True)
    tmp_xdg.write_text("{not valid json")
    # Don't crash — corrupt file means treat as empty.
    cfg = LastConfig()
    assert cfg.load("emfi") == {}


def test_xdg_default_when_env_unset(monkeypatch, tmp_path):
    """Without XDG_CONFIG_HOME, defaults to ~/.config/faultycmd/."""
    monkeypatch.delenv("XDG_CONFIG_HOME", raising=False)
    monkeypatch.setenv("HOME", str(tmp_path))
    cfg = LastConfig()
    expected = tmp_path / ".config" / "faultycmd" / "last_config.json"
    assert cfg.path == expected


def test_xdg_respects_env_override(monkeypatch, tmp_path):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "alt"))
    cfg = LastConfig()
    assert cfg.path == tmp_path / "alt" / "faultycmd" / "last_config.json"


def test_save_writes_atomic(tmp_xdg):
    """Concurrent reads must never see a half-written file. We
    achieve this by writing to a sibling tmp file then os.replace."""
    cfg = LastConfig()
    cfg.save("emfi", {"width_us": 5})
    # No leftover .tmp files.
    siblings = list(tmp_xdg.parent.glob("last_config.json.*"))
    assert siblings == [], f"leftover atomic-write tmp: {siblings}"

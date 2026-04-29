"""Smoke tests for faultycmd.tui — DiagSnapshot regex + panel
state mutations.

We deliberately avoid Textual's `Pilot` async test framework here —
its internal lifecycle relies on a fully-bound `Worker` system that
proved brittle across pytest-asyncio versions when the App also
spins up its own threading.Threads. The real-device smoke documented
in the F10-5 commit message exercises the App end-to-end against a
live FaultyCat instead.
"""
from __future__ import annotations

from faultycmd.tui import (
    CampaignPanel,
    DiagSnapshot,
    FaultycmdTUI,
    StatusPanel,
)

# -- DiagSnapshot parser ------------------------------------------

def test_diag_snapshot_parses_real_line():
    line = "ADC= 757 SCAN=11111111 TRIG=0 GATE=NONE HV[---] EMFI=IDLE CROW=IDLE"
    snap = DiagSnapshot.parse(line)
    assert snap is not None
    assert snap.adc == 757
    assert snap.scan == "11111111"
    assert snap.trig == 0
    assert snap.gate == "NONE"
    assert snap.hv == "---"
    assert snap.emfi == "IDLE"
    assert snap.crow == "IDLE"
    assert snap.last_seen_at > 0.0


def test_diag_snapshot_parses_armed_charged_hp():
    # Mid-sweep / post-fire snapshot.
    line = "ADC= 745 SCAN=11111101 TRIG=1 GATE=HP   HV[ARM CHG] EMFI=CHARGED CROW=FIRED"
    snap = DiagSnapshot.parse(line)
    assert snap is not None
    assert snap.gate == "HP"
    assert snap.hv == "ARM CHG"
    assert snap.emfi == "CHARGED"
    assert snap.crow == "FIRED"
    assert snap.trig == 1


def test_diag_snapshot_returns_none_on_garbage():
    assert DiagSnapshot.parse("a totally unrelated line of text") is None
    assert DiagSnapshot.parse("") is None
    assert DiagSnapshot.parse("ADC=") is None


def test_diag_snapshot_ignores_prefix_noise():
    # The CDC2 stream may interleave shell echoes / blank lines /
    # banner fragments. The regex anchors on `ADC=` so prefix junk
    # before the marker is ignored.
    line = "OK init swclk=GP0 swdio=GP1 nrst=2  ADC=  10 SCAN=10000000 TRIG=0 GATE=LP   HV[---]    EMFI=IDLE CROW=IDLE"
    snap = DiagSnapshot.parse(line)
    assert snap is not None
    assert snap.adc == 10
    assert snap.gate == "LP"


# -- StatusPanel ---------------------------------------------------
# Tests inspect the data side (`.fields`, `.title_text`, `.tail`)
# rather than the Textual-rendered string. The rendered output is
# exercised by Textual's own internal tests + by the F10-5
# real-device smoke documented in the commit message.

def test_status_panel_initial_state():
    p = StatusPanel("EMFI")
    assert p.title_text == "EMFI"
    assert p.fields == {}


def test_status_panel_update_fields_stores_dict():
    p = StatusPanel("EMFI")
    p.update_fields({"state": "IDLE", "err": "NONE", "last_fire_ms": "0"})
    assert p.fields["state"] == "IDLE"
    assert p.fields["err"] == "NONE"
    assert p.fields["last_fire_ms"] == "0"


def test_status_panel_update_replaces_not_merges():
    p = StatusPanel("EMFI")
    p.update_fields({"state": "IDLE"})
    p.update_fields({"state": "FIRED", "err": "NONE"})
    assert p.fields == {"state": "FIRED", "err": "NONE"}


# -- CampaignPanel -------------------------------------------------

def test_campaign_panel_initial_state():
    p = CampaignPanel()
    assert p.summary == {}
    assert p.tail == []


def test_campaign_panel_summary_and_tail():
    p = CampaignPanel()
    p.set_summary({"state": "SWEEPING", "step": "2/6", "pushed": "2"})
    p.push_results([
        "step=0 d=1000 w=200 p=1 fire=0x00 verify=0x00",
        "step=1 d=1000 w=300 p=1 fire=0x00 verify=0x00",
    ])
    assert p.summary["state"] == "SWEEPING"
    assert len(p.tail) == 2
    assert "step=0" in p.tail[0]


def test_campaign_panel_tail_caps_at_max():
    p = CampaignPanel()
    for i in range(p.MAX_TAIL + 5):
        p.push_results([f"step={i}"])
    assert len(p.tail) == p.MAX_TAIL
    # Newest results win.
    assert p.tail[-1] == f"step={p.MAX_TAIL + 4}"


def test_campaign_panel_clear_tail():
    p = CampaignPanel()
    p.push_results(["step=0", "step=1"])
    p.clear_tail()
    assert p.tail == []


# -- App construction (no run) ------------------------------------

def test_app_construction_does_not_open_serial():
    """Constructing the App must not touch any CDC — opening happens
    in on_mount once Textual is running. This keeps `import
    faultycmd.tui` lightweight enough for headless smoke (pytest,
    `faultycmd --version`, etc.) on machines without a board."""
    app = FaultycmdTUI()
    # No connections opened yet.
    assert app.conn.emfi is None
    assert app.conn.crowbar is None
    assert app.conn.campaign is None
    assert app.conn.cdc1_shared is None
    # Bindings declared.
    binding_keys = {b.key for b in app.BINDINGS}
    assert binding_keys >= {"q", "r", "c", "s"}


def test_app_title_set():
    app = FaultycmdTUI()
    assert "faultycmd" in app.title.lower()


def test_app_does_not_shadow_textual_workers():
    """Regression: a previous revision named the polling-thread list
    ``self._workers`` which clobbered Textual's ``App._workers`` (the
    backing field of ``App.workers`` → ``WorkerManager``). On unmount
    every Static panel does ``self.workers.cancel_node(self)``; with
    the field shadowed by a list of threads that raised
    ``AttributeError: 'list' object has no attribute 'cancel_node'``
    and propagated as ``RuntimeError: Event loop is closed`` from the
    daemon threads. Crash on launch / on `q`."""
    from textual.worker_manager import WorkerManager

    app = FaultycmdTUI()
    assert isinstance(app.workers, WorkerManager)
    assert hasattr(app.workers, "cancel_node")
    # Our own thread bookkeeping must not collide with the framework's.
    assert isinstance(app._poll_threads, list)

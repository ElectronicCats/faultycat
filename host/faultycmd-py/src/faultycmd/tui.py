"""F10-5 — Textual dashboard.

Four-panel 2×2 grid + hotkeys footer:

    ┌─────────────────────────┬─────────────────────────┐
    │ EMFI status   (CDC0)    │ Crowbar status (CDC1)   │
    ├─────────────────────────┼─────────────────────────┤
    │ Campaign live (CDC1)    │ Diag snapshot (CDC2)    │
    └─────────────────────────┴─────────────────────────┘
                  q quit · r reconnect · c clear · s start/stop demo

Each panel polls independently:

    EMFI:     emfi_proto STATUS over CDC0   every 500 ms
    Crowbar:  crowbar_proto STATUS over CDC1 every 500 ms
    Campaign: campaign_proto STATUS + DRAIN  every 500 ms
              (shares CDC1 with Crowbar via a single serial.Serial
               handed to both clients through serial_factory)
    Diag:     CDC2 diag snapshot tail        line-driven

Hotkeys:
    q   — quit
    r   — close all CDCs and re-open (handy after re-flashing)
    c   — clear the campaign live log
    s   — start a 6-step demo crowbar campaign on the configured
          engine, or stop the running one.

CDC ownership during a TUI session:
    CDC0 is held by EMFI panel.
    CDC1 is held by Crowbar + Campaign panels (shared Serial).
    CDC2 is held read-only by Diag panel — the operator should
        NOT run `faultycmd scanner ...` or open picocom against
        CDC2 simultaneously, or both readers will fight for bytes.
        Use the CLI in a separate terminal session instead, after
        quitting the TUI.
"""
from __future__ import annotations

import re
import threading
import time
from dataclasses import dataclass

from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Grid
from textual.reactive import reactive
from textual.widgets import Footer, Header, Static

from .persistence import LastConfig
from .protocols import (
    CampaignClient,
    CampaignError,
    CrowbarClient,
    EmfiClient,
    EngineError,
    ProtocolError,
)
from .protocols.campaign import CampaignState
from .tui_modals import (
    EmfiControlModal,
    EmfiFormState,
    HvConfirmModal,
)
from .usb import PortDiscoveryError, cdc_for

# -----------------------------------------------------------------------------
# Diag snapshot parser — matches the line emitted every 500 ms by
# `apps/faultycat_fw/main.c::print_snapshot`:
#
#   ADC= 757 SCAN=11111111 TRIG=0 GATE=NONE HV[---] EMFI=IDLE CROW=IDLE
# -----------------------------------------------------------------------------

DIAG_RE = re.compile(
    r"ADC=\s*(?P<adc>\d+)\s+"
    r"SCAN=(?P<scan>[01]+)\s+"
    r"TRIG=(?P<trig>\d)\s+"
    r"GATE=(?P<gate>\S+)\s+"
    r"HV\[(?P<hv>.{0,8}?)\]\s+"
    r"EMFI=(?P<emfi>\S+)\s+"
    r"CROW=(?P<crow>\S+)"
)


@dataclass
class DiagSnapshot:
    adc: int = 0
    scan: str = ""
    trig: int = 0
    gate: str = "?"
    hv: str = "?"
    emfi: str = "?"
    crow: str = "?"
    last_seen_at: float = 0.0

    @classmethod
    def parse(cls, line: str) -> DiagSnapshot | None:
        m = DIAG_RE.search(line)
        if not m:
            return None
        return cls(
            adc=int(m.group("adc")),
            scan=m.group("scan"),
            trig=int(m.group("trig")),
            gate=m.group("gate"),
            hv=m.group("hv").strip(),
            emfi=m.group("emfi"),
            crow=m.group("crow"),
            last_seen_at=time.time(),
        )


# -----------------------------------------------------------------------------
# Shared CDC1 serial (Crowbar + Campaign use the same port — one
# serial.Serial, two clients via the protocol-base serial_factory hook).
# -----------------------------------------------------------------------------


class SharedSerial:
    """Wrap a single :class:`serial.Serial` so both the crowbar and
    campaign clients can hand the same instance to their base class.

    The protocol clients call read / write / reset_input_buffer /
    close serially (Textual workers don't preempt each other on the
    same thread), so we don't need a mutex around the underlying
    Serial — but we DO need to lie about close: the first client to
    leave its context manager would otherwise tear the port down.
    """

    def __init__(self, ser: object) -> None:
        self._ser = ser

    def write(self, data: bytes) -> int:
        return self._ser.write(data)   # type: ignore[attr-defined]

    def read(self, size: int = 1) -> bytes:
        return self._ser.read(size)   # type: ignore[attr-defined]

    def reset_input_buffer(self) -> None:
        self._ser.reset_input_buffer()   # type: ignore[attr-defined]

    def close(self) -> None:
        # Owned by FaultycmdTUI, not the per-client context manager.
        pass

    def really_close(self) -> None:
        self._ser.close()   # type: ignore[attr-defined]


# -----------------------------------------------------------------------------
# Connection bundle — opened/closed by the App lifecycle
# -----------------------------------------------------------------------------


@dataclass
class Connections:
    emfi: EmfiClient | None = None
    crowbar: CrowbarClient | None = None
    campaign: CampaignClient | None = None
    cdc1_shared: SharedSerial | None = None
    cdc2_serial: object | None = None       # serial.Serial in diag tail
    last_error: str = ""

    def open(self) -> None:
        # Lazy import — keep `serial` out of the test's import surface.
        import serial  # noqa: PLC0415

        self.last_error = ""
        try:
            self.emfi = EmfiClient(cdc_for("emfi"))
            self.emfi.open()

            cdc1 = serial.Serial(cdc_for("crowbar"), 115200, timeout=0.5)
            self.cdc1_shared = SharedSerial(cdc1)

            crowbar_factory = lambda *_a, **_kw: self.cdc1_shared   # noqa: E731
            self.crowbar = CrowbarClient("/dev/null", serial_factory=crowbar_factory)
            self.crowbar.open()

            campaign_factory = lambda *_a, **_kw: self.cdc1_shared  # noqa: E731
            self.campaign = CampaignClient(
                "/dev/null", engine="crowbar",
                serial_factory=campaign_factory,
            )
            self.campaign.open()

            self.cdc2_serial = serial.Serial(cdc_for("scanner"), 115200, timeout=0.2)
        except (PortDiscoveryError, OSError) as e:
            self.last_error = f"open: {e}"
            self.close()

    def close(self) -> None:
        if self.emfi:
            self.emfi.close()
        self.emfi = None
        # crowbar/campaign share cdc1; release them then close the shared port.
        self.crowbar = None
        self.campaign = None
        if self.cdc1_shared:
            self.cdc1_shared.really_close()
        self.cdc1_shared = None
        if self.cdc2_serial:
            self.cdc2_serial.close()
        self.cdc2_serial = None


# -----------------------------------------------------------------------------
# Panels
# -----------------------------------------------------------------------------


class StatusPanel(Static):
    """Base for the 4 panels — stores a title + dict of fields and
    re-renders to a tidy text block on update."""

    DEFAULT_CSS = """
    StatusPanel {
        border: round $accent;
        padding: 0 1;
    }
    """

    def __init__(self, title: str, *, classes: str = "") -> None:
        super().__init__(classes=classes)
        self.title_text = title
        self.fields: dict[str, str] = {}
        self._render_self()

    def update_fields(self, fields: dict[str, str]) -> None:
        self.fields = fields
        self._render_self()

    def _render_self(self) -> None:
        lines = [f"[bold cyan]{self.title_text}[/bold cyan]"]
        if not self.fields:
            lines.append("[dim](no data yet)[/dim]")
        else:
            width = max((len(k) for k in self.fields), default=0)
            for k, v in self.fields.items():
                lines.append(f"  [cyan]{k:<{width}}[/cyan]  {v}")
        self.update("\n".join(lines))


class CampaignPanel(Static):
    """Like :class:`StatusPanel` but with a tail-window of recent
    campaign results in addition to the summary fields."""

    DEFAULT_CSS = """
    CampaignPanel {
        border: round $accent;
        padding: 0 1;
    }
    """

    MAX_TAIL = 10

    def __init__(self) -> None:
        super().__init__()
        self.summary: dict[str, str] = {}
        self.tail: list[str] = []
        self._render_self()

    def set_summary(self, fields: dict[str, str]) -> None:
        self.summary = fields
        self._render_self()

    def push_results(self, lines: list[str]) -> None:
        self.tail.extend(lines)
        if len(self.tail) > self.MAX_TAIL:
            self.tail = self.tail[-self.MAX_TAIL:]
        self._render_self()

    def clear_tail(self) -> None:
        self.tail.clear()
        self._render_self()

    def _render_self(self) -> None:
        lines = ["[bold cyan]Campaign[/bold cyan]"]
        if self.summary:
            width = max((len(k) for k in self.summary), default=0)
            for k, v in self.summary.items():
                lines.append(f"  [cyan]{k:<{width}}[/cyan]  {v}")
        else:
            lines.append("[dim](no data yet)[/dim]")
        lines.append("")
        lines.append(f"[dim]last {self.MAX_TAIL} results:[/dim]")
        if self.tail:
            for entry in self.tail:
                lines.append(f"  {entry}")
        else:
            lines.append("  [dim](empty)[/dim]")
        self.update("\n".join(lines))


# -----------------------------------------------------------------------------
# App
# -----------------------------------------------------------------------------


class FaultycmdTUI(App[None]):
    """Live FaultyCat dashboard."""

    CSS = """
    Grid {
        grid-size: 2 2;
        grid-gutter: 1;
        padding: 0 1;
    }
    """

    BINDINGS = [
        Binding("q", "quit", "quit"),
        Binding("r", "reconnect", "reconnect"),
        Binding("c", "clear_log", "clear log"),
        Binding("s", "toggle_demo", "start/stop demo"),
        Binding("e", "open_emfi_modal", "EMFI control"),
    ]

    title = "faultycmd — FaultyCat v3 dashboard"

    last_error: reactive[str] = reactive("")

    def __init__(self) -> None:
        super().__init__()
        self.conn = Connections()
        self.emfi_panel: StatusPanel | None = None
        self.crowbar_panel: StatusPanel | None = None
        self.campaign_panel: CampaignPanel | None = None
        self.diag_panel: StatusPanel | None = None
        self._stop_workers = threading.Event()
        self._poll_threads: list[threading.Thread] = []
        self._demo_running = False
        self._last_config = LastConfig()

    # -- compose ------------------------------------------------------

    def compose(self) -> ComposeResult:
        yield Header()
        self.emfi_panel = StatusPanel("EMFI")
        self.crowbar_panel = StatusPanel("Crowbar")
        self.campaign_panel = CampaignPanel()
        self.diag_panel = StatusPanel("Diag (CDC2 tail)")
        with Grid():
            yield self.emfi_panel
            yield self.crowbar_panel
            yield self.campaign_panel
            yield self.diag_panel
        yield Footer()

    # -- lifecycle ----------------------------------------------------

    def on_mount(self) -> None:
        self.conn.open()
        if self.conn.last_error:
            self.notify(self.conn.last_error, severity="error")
            return
        self._spawn_workers()

    def on_unmount(self) -> None:
        self._stop_workers.set()
        for t in self._poll_threads:
            t.join(timeout=1.0)
        self.conn.close()

    # -- workers ------------------------------------------------------

    def _post(self, fn, *args) -> None:
        """Bridge daemon-thread → Textual UI thread. Swallows the
        RuntimeError that Textual raises when ``call_from_thread`` is
        invoked after the asyncio loop has started shutting down — the
        polling threads can race past ``_stop_workers.set()`` and try
        one more call before joining."""
        try:
            self.call_from_thread(fn, *args)
        except RuntimeError:
            pass

    def _spawn_workers(self) -> None:
        self._stop_workers.clear()
        self._poll_threads = [
            threading.Thread(target=self._poll_emfi, daemon=True),
            threading.Thread(target=self._poll_cdc1, daemon=True),
            threading.Thread(target=self._tail_diag, daemon=True),
        ]
        for t in self._poll_threads:
            t.start()

    def _poll_emfi(self) -> None:
        while not self._stop_workers.is_set():
            if not self.conn.emfi:
                time.sleep(0.5)
                continue
            try:
                st = self.conn.emfi.status()
                self._post(self._update_emfi, st)
            except (ProtocolError, EngineError, OSError) as e:
                self._post(self._note_error, f"emfi: {e}")
            self._stop_workers.wait(0.5)

    def _poll_cdc1(self) -> None:
        while not self._stop_workers.is_set():
            if not (self.conn.crowbar and self.conn.campaign):
                time.sleep(0.5)
                continue
            try:
                cst = self.conn.crowbar.status()
                self._post(self._update_crowbar, cst)
                camp_st = self.conn.campaign.status()
                self._post(self._update_campaign_summary, camp_st)
                if camp_st.state in (CampaignState.SWEEPING, CampaignState.DONE):
                    results = self.conn.campaign.drain(18)
                    if results:
                        rendered = [
                            f"step={r.step_n} d={r.delay} w={r.width} "
                            f"p={r.power} fire=0x{r.fire_status:02X} "
                            f"verify=0x{r.verify_status:02X}"
                            for r in results
                        ]
                        self._post(self._push_campaign_results, rendered)
            except (ProtocolError, EngineError, CampaignError, OSError) as e:
                self._post(self._note_error, f"cdc1: {e}")
            self._stop_workers.wait(0.5)

    def _tail_diag(self) -> None:
        buf = ""
        while not self._stop_workers.is_set():
            if not self.conn.cdc2_serial:
                time.sleep(0.5)
                continue
            try:
                chunk = self.conn.cdc2_serial.read(256)   # type: ignore[attr-defined]
                if not chunk:
                    time.sleep(0.05)
                    continue
                buf += chunk.decode(errors="replace")
                while "\n" in buf:
                    line, _, buf = buf.partition("\n")
                    snap = DiagSnapshot.parse(line)
                    if snap is not None:
                        self._post(self._update_diag, snap)
            except OSError as e:
                self._post(self._note_error, f"diag: {e}")
                self._stop_workers.wait(1.0)

    # -- panel updates (run on Textual thread) ------------------------

    def _update_emfi(self, st) -> None:
        if self.emfi_panel is None:
            return
        self.emfi_panel.update_fields({
            "state": st.state.name if hasattr(st.state, "name") else str(st.state),
            "err": st.err.name if hasattr(st.err, "name") else str(st.err),
            "last_fire_ms": str(st.last_fire_at_ms),
            "capture_fill": str(st.capture_fill),
            "width_us":     str(st.pulse_width_us_actual),
            "delay_us":     str(st.delay_us_actual),
        })

    def _update_crowbar(self, st) -> None:
        if self.crowbar_panel is None:
            return
        self.crowbar_panel.update_fields({
            "state":  st.state.name if hasattr(st.state, "name") else str(st.state),
            "err":    st.err.name if hasattr(st.err, "name") else str(st.err),
            "last_fire_ms": str(st.last_fire_at_ms),
            "width_ns":     str(st.pulse_width_ns_actual),
            "delay_us":     str(st.delay_us_actual),
            "output":       st.output.name if hasattr(st.output, "name") else str(st.output),
        })

    def _update_campaign_summary(self, st) -> None:
        if self.campaign_panel is None:
            return
        self.campaign_panel.set_summary({
            "state":   st.state.name if hasattr(st.state, "name") else str(st.state),
            "err":     st.err.name if hasattr(st.err, "name") else str(st.err),
            "step":    f"{st.step_n}/{st.total_steps}",
            "pushed":  str(st.results_pushed),
            "dropped": str(st.results_dropped),
        })

    def _push_campaign_results(self, rendered: list[str]) -> None:
        if self.campaign_panel is None:
            return
        self.campaign_panel.push_results(rendered)

    def _update_diag(self, snap: DiagSnapshot) -> None:
        if self.diag_panel is None:
            return
        self.diag_panel.update_fields({
            "ADC":  str(snap.adc),
            "SCAN": snap.scan,
            "TRIG": str(snap.trig),
            "GATE": snap.gate,
            "HV":   snap.hv if snap.hv else "---",
            "EMFI": snap.emfi,
            "CROW": snap.crow,
        })

    def _note_error(self, msg: str) -> None:
        self.notify(msg, severity="warning", timeout=2)

    # -- hotkeys ------------------------------------------------------

    def action_reconnect(self) -> None:
        self._stop_workers.set()
        for t in self._poll_threads:
            t.join(timeout=1.0)
        self.conn.close()
        self.conn.open()
        if self.conn.last_error:
            self.notify(self.conn.last_error, severity="error")
            return
        self._spawn_workers()
        self.notify("reconnected", severity="information")

    def action_clear_log(self) -> None:
        if self.campaign_panel is not None:
            self.campaign_panel.clear_tail()

    def action_toggle_demo(self) -> None:
        if not self.conn.campaign:
            self.notify("no campaign client", severity="error")
            return
        try:
            if self._demo_running:
                self.conn.campaign.stop()
                self._demo_running = False
                self.notify("demo stopped", severity="information")
            else:
                self.conn.campaign.configure(
                    delay=(1000, 3000, 1000),
                    width=(200, 300, 100),
                    power=(1, 1, 0),
                    settle_ms=50,
                )
                self.conn.campaign.start()
                self._demo_running = True
                self.notify("demo started — 6-step crowbar LP sweep", severity="information")
        except (CampaignError, EngineError, ProtocolError, OSError) as e:
            self.notify(f"demo: {e}", severity="error")

    def action_open_emfi_modal(self) -> None:
        if not self.conn.emfi:
            self.notify("no EMFI client (CDC0)", severity="error")
            return
        initial = EmfiFormState.from_dict(self._last_config.load("emfi"))

        def _on_apply(state: EmfiFormState) -> None:
            self.conn.emfi.configure(
                trigger=state.trigger,
                delay_us=state.delay_us,
                width_us=state.width_us,
                charge_timeout_ms=state.charge_timeout_ms,
            )
            self._last_config.save("emfi", state.to_dict())

        def _on_arm(state: EmfiFormState) -> None:
            self.conn.emfi.arm()

        def _on_fire() -> None:
            self.conn.emfi.fire()

        def _on_disarm() -> None:
            self.conn.emfi.disarm()

        def _on_capture() -> None:
            # F11-0a ships a Rich-table summary in the modal status
            # line (no plot — that's v3.1 GUI / F12). We pull a small
            # slice and render counts; the dashboard EMFI panel keeps
            # its existing capture_fill field for at-a-glance.
            slice_ = self.conn.emfi.capture(offset=0, n=64)
            self.notify(
                f"capture: {len(slice_)} samples (peak={max(slice_) if slice_ else 0})",
                severity="information",
            )

        def _confirm_arm(after) -> None:
            """Push HvConfirmModal; on dismiss invoke `after(bool)`."""
            self.push_screen(
                HvConfirmModal(action_label="Arm EMFI"),
                callback=after,
            )

        modal = EmfiControlModal(
            initial=initial,
            apply_cb=_on_apply,
            arm_cb=_on_arm,
            fire_cb=_on_fire,
            disarm_cb=_on_disarm,
            capture_cb=_on_capture,
            confirm_arm_cb=_confirm_arm,
        )
        self.push_screen(modal)


# -----------------------------------------------------------------------------
# Public entry point — used by faultycmd.cli's `tui` command.
# -----------------------------------------------------------------------------


def run() -> None:
    """Launch the dashboard. Returns when the user quits with ``q``."""
    FaultycmdTUI().run()

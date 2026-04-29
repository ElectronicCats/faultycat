"""F11-0a — Textual control modals.

The dashboard `FaultycmdTUI` opens these via hotkey:

    e → EmfiControlModal       (CDC0)
    [c → CrowbarControlModal      F11-0b]
    [p → CampaignControlModal     F11-0c]
    [k → ScannerControlModal      F11-0d]

The HV confirm modal is interposed for any action that charges
the HV cap (currently: EMFI `arm`). This is the safety gate
F10-polish flagged as missing — accidental focus traversal could
otherwise arm without an explicit operator decision.

These modules are kept ``Pilot``-free — see ``test_tui_modals.py``
docstring for the rationale (Pilot conflicts with our daemon-
thread shutdown machinery; we test data state, not rendered output).
"""
from __future__ import annotations

from dataclasses import asdict, dataclass, fields

from textual.app import ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, Vertical
from textual.screen import ModalScreen
from textual.widgets import Button, Input, Label, Select, Static

# -----------------------------------------------------------------
# EMFI form state
# -----------------------------------------------------------------


_EMFI_TRIGGERS = ("immediate", "ext_rising", "ext_falling", "ext_pulse_pos")


@dataclass
class EmfiFormState:
    trigger: str = "immediate"
    delay_us: int = 0
    width_us: int = 5
    charge_timeout_ms: int = 0

    @classmethod
    def from_dict(cls, d: dict) -> EmfiFormState:
        # Filter unknown keys + fill defaults for missing.
        known = {f.name for f in fields(cls)}
        kwargs = {k: v for k, v in d.items() if k in known}
        return cls(**kwargs)

    def to_dict(self) -> dict:
        return asdict(self)

    def validate(self) -> None:
        if self.trigger not in _EMFI_TRIGGERS:
            raise ValueError(
                f"trigger must be one of {_EMFI_TRIGGERS}, got {self.trigger!r}"
            )
        if not 1 <= self.width_us <= 50:
            raise ValueError(
                f"width_us out of range 1..50 µs (driver-bounded): {self.width_us}"
            )
        if self.delay_us < 0:
            raise ValueError(f"delay_us must be >= 0, got {self.delay_us}")
        if self.charge_timeout_ms < 0:
            raise ValueError(
                f"charge_timeout_ms must be >= 0, got {self.charge_timeout_ms}"
            )


# -----------------------------------------------------------------
# HV confirm
# -----------------------------------------------------------------


class HvConfirmModal(ModalScreen[bool]):
    """Yes/No confirm before any action that charges the HV cap.
    Default focus is on No so a stray Enter is not destructive."""

    DEFAULT_CSS = """
    HvConfirmModal > Vertical {
        background: $panel;
        border: thick $error;
        padding: 1 2;
        width: 60;
        height: auto;
        align: center middle;
    }
    """

    BINDINGS = [
        Binding("escape", "decide(False)", "no"),
    ]

    default_decision = False

    def __init__(self, *, action_label: str) -> None:
        super().__init__()
        self.action_label = action_label

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(f"⚠  HV safety gate: {self.action_label}")
            yield Label(
                "[dim]This will charge the high-voltage capacitor.\n"
                "Confirm only if the device is in a safe configuration.[/dim]"
            )
            with Horizontal():
                yield Button("No (default)", id="no", variant="default")
                yield Button("Yes — proceed", id="yes", variant="error")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(event.button.id == "yes")

    def action_decide(self, value: bool) -> None:
        self.dismiss(value)


# -----------------------------------------------------------------
# EMFI control modal
# -----------------------------------------------------------------


_HV_CONFIRM_ACTIONS = frozenset({"arm"})


class EmfiControlModal(ModalScreen[None]):
    """EMFI configure / arm / fire / disarm + capture viewer.

    The modal owns its own form state (`self.state: EmfiFormState`)
    plus a callback bundle (`apply_cb / arm_cb / fire_cb / disarm_cb /
    capture_cb`) so it can be unit-tested without spinning up a real
    EmfiClient. The dashboard wires the callbacks to the live client
    when it pushes the modal."""

    DEFAULT_CSS = """
    EmfiControlModal > Vertical {
        background: $panel;
        border: thick $accent;
        padding: 1 2;
        width: 80;
        height: auto;
    }
    EmfiControlModal Input { width: 100%; }
    EmfiControlModal Select { width: 100%; }
    """

    BINDINGS = [
        Binding("escape", "close", "close"),
    ]

    def __init__(
        self,
        *,
        initial: EmfiFormState | None = None,
        apply_cb=None,
        arm_cb=None,
        fire_cb=None,
        disarm_cb=None,
        capture_cb=None,
        confirm_arm_cb=None,
    ) -> None:
        super().__init__()
        self.state = initial or EmfiFormState()
        self.apply_cb = apply_cb
        self.arm_cb = arm_cb
        self.fire_cb = fire_cb
        self.disarm_cb = disarm_cb
        self.capture_cb = capture_cb
        self.confirm_arm_cb = confirm_arm_cb

    @staticmethod
    def requires_hv_confirm(action: str) -> bool:
        return action in _HV_CONFIRM_ACTIONS

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label("EMFI control")
            yield Label("[dim]CDC0 · F4 emfi_proto[/dim]")
            yield Label("trigger:")
            yield Select(
                [(t, t) for t in _EMFI_TRIGGERS],
                value=self.state.trigger,
                id="trigger",
            )
            yield Label("delay-us:")
            yield Input(value=str(self.state.delay_us), id="delay_us")
            yield Label("width-us (1..50):")
            yield Input(value=str(self.state.width_us), id="width_us")
            yield Label("charge-timeout-ms (0 = use 60 s budget):")
            yield Input(
                value=str(self.state.charge_timeout_ms), id="charge_timeout_ms"
            )
            yield Static("", id="status_line")
            with Horizontal():
                yield Button("Apply", id="apply", variant="primary")
                yield Button("Arm", id="arm", variant="warning")
                yield Button("Fire", id="fire", variant="success")
                yield Button("Disarm", id="disarm")
                yield Button("Capture", id="capture")
                yield Button("Close", id="close")

    # -- form sync --------------------------------------------------

    def _sync_state_from_inputs(self) -> bool:
        """Pull current widget values into self.state. Returns True
        on success, False on parse error (state untouched)."""
        try:
            trig = self.query_one("#trigger", Select).value
            d = int(self.query_one("#delay_us", Input).value or "0")
            w = int(self.query_one("#width_us", Input).value or "0")
            ct = int(self.query_one("#charge_timeout_ms", Input).value or "0")
        except (ValueError, KeyError):
            self._set_status("error: invalid integer in form")
            return False
        candidate = EmfiFormState(
            trigger=trig if isinstance(trig, str) else "immediate",
            delay_us=d, width_us=w, charge_timeout_ms=ct,
        )
        try:
            candidate.validate()
        except ValueError as e:
            self._set_status(f"error: {e}")
            return False
        self.state = candidate
        return True

    def _set_status(self, msg: str) -> None:
        try:
            self.query_one("#status_line", Static).update(msg)
        except Exception:
            pass

    # -- actions ---------------------------------------------------

    def on_button_pressed(self, event: Button.Pressed) -> None:
        action = event.button.id or ""
        if action == "close":
            self.action_close()
            return
        if action in ("apply", "arm"):
            if not self._sync_state_from_inputs():
                return
        if action == "apply" and self.apply_cb:
            try:
                self.apply_cb(self.state)
                self._set_status("OK applied")
            except Exception as e:
                self._set_status(f"apply: {e}")
        elif action == "arm":
            if self.confirm_arm_cb is None or not callable(self.confirm_arm_cb):
                # No confirm hook wired (e.g. unit test) — refuse.
                self._set_status("error: HV confirm not wired")
                return
            self.confirm_arm_cb(self._do_arm_post_confirm)
        elif action == "fire" and self.fire_cb:
            try:
                self.fire_cb()
                self._set_status("OK fire")
            except Exception as e:
                self._set_status(f"fire: {e}")
        elif action == "disarm" and self.disarm_cb:
            try:
                self.disarm_cb()
                self._set_status("OK disarm")
            except Exception as e:
                self._set_status(f"disarm: {e}")
        elif action == "capture" and self.capture_cb:
            try:
                self.capture_cb()
                self._set_status("OK capture (see panel)")
            except Exception as e:
                self._set_status(f"capture: {e}")

    def _do_arm_post_confirm(self, confirmed: bool) -> None:
        if not confirmed or self.arm_cb is None:
            self._set_status("arm cancelled" if not confirmed else "arm: no client")
            return
        try:
            self.arm_cb(self.state)
            self._set_status("OK arm")
        except Exception as e:
            self._set_status(f"arm: {e}")

    def action_close(self) -> None:
        self.dismiss(None)


# -----------------------------------------------------------------
# Campaign form state + control modal (F11-0c MVP, engine=crowbar)
# -----------------------------------------------------------------


_CAMPAIGN_ENGINES = ("crowbar",)   # F11-0c MVP — emfi multiplex deferred


def parse_triplet(s: str) -> tuple[int, int, int]:
    """Accept ``"START:END:STEP"`` or a single ``"N"`` (collapses
    axis). Returns ``(start, end, step)``; raises ValueError on a
    malformed input or a non-monotonic / negative span."""
    parts = s.strip().split(":")
    if len(parts) == 1:
        n = int(parts[0])
        return (n, n, 0)
    if len(parts) != 3:
        raise ValueError(
            f"triplet must be 'START:END:STEP' or single 'N', got {s!r}"
        )
    start, end, step = (int(p) for p in parts)
    if start > end:
        raise ValueError(f"triplet start ({start}) must be <= end ({end})")
    if start != end and step <= 0:
        raise ValueError(
            f"triplet step must be > 0 when start ({start}) != end ({end})"
        )
    return (start, end, step)


@dataclass
class CampaignFormState:
    engine:    str = "crowbar"
    delay:     str = "1000:3000:1000"   # µs (text triplet, parsed on validate)
    width:     str = "200:300:100"      # ns for crowbar, µs for emfi
    power:     str = "1:1:0"            # crowbar 1=LP / 2=HP
    settle_ms: int = 50

    @classmethod
    def from_dict(cls, d: dict) -> CampaignFormState:
        known = {f.name for f in fields(cls)}
        kwargs = {k: v for k, v in d.items() if k in known}
        return cls(**kwargs)

    def to_dict(self) -> dict:
        return asdict(self)

    def parse(self) -> tuple[
        tuple[int, int, int], tuple[int, int, int], tuple[int, int, int], int
    ]:
        """Resolve the text triplets to wire-level tuples. Raises
        ValueError on any axis-parse failure."""
        return (
            parse_triplet(self.delay),
            parse_triplet(self.width),
            parse_triplet(self.power),
            self.settle_ms,
        )

    def validate(self) -> None:
        if self.engine not in _CAMPAIGN_ENGINES:
            raise ValueError(
                f"engine must be one of {_CAMPAIGN_ENGINES}, got {self.engine!r}"
            )
        if not 0 <= self.settle_ms <= 60000:
            raise ValueError(
                f"settle_ms out of range 0..60000, got {self.settle_ms}"
            )
        # Trip every axis through parse to surface a malformed
        # triplet at validate time (not deep inside configure).
        self.parse()


class CampaignControlModal(ModalScreen[None]):
    """Campaign full-sweep configure / start / stop / drain.

    Replaces the F10 dashboard's `s` toggle-demo (locked to a
    6-step crowbar LP sweep). F11-0c MVP keeps the engine fixed
    to crowbar — emfi multiplex needs a `Connections` refactor
    (CDC0 SharedSerial wrapper + retrofit `EmfiClient` to use
    `serial_factory`) that's deferred to F-future."""

    DEFAULT_CSS = """
    CampaignControlModal > Vertical {
        background: $panel;
        border: thick $accent;
        padding: 1 2;
        width: 90;
        height: auto;
    }
    CampaignControlModal Input { width: 100%; }
    CampaignControlModal Select { width: 100%; }
    """

    BINDINGS = [
        Binding("escape", "close", "close"),
    ]

    def __init__(
        self,
        *,
        initial: CampaignFormState | None = None,
        configure_cb=None,
        start_cb=None,
        stop_cb=None,
        drain_cb=None,
    ) -> None:
        super().__init__()
        self.state = initial or CampaignFormState()
        self.configure_cb = configure_cb
        self.start_cb = start_cb
        self.stop_cb = stop_cb
        self.drain_cb = drain_cb

    @staticmethod
    def requires_hv_confirm(action: str) -> bool:
        # Campaign drives the engine which may charge HV (when
        # engine=emfi); F11-0c MVP only supports crowbar so no
        # confirm is needed yet. When emfi multiplex lands, this
        # should return True for `start` if engine == "emfi".
        return False

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label("Campaign control")
            yield Label("[dim]F9 campaign_proto · multiplex CDC0/CDC1[/dim]")
            yield Label("engine:")
            yield Select(
                [(e.upper(), e) for e in _CAMPAIGN_ENGINES],
                value=self.state.engine,
                id="engine",
            )
            yield Label("[dim](emfi multiplex: F-future — needs Connections refactor)[/dim]")
            yield Label("delay (µs)  START:END:STEP or single int:")
            yield Input(value=self.state.delay, id="delay")
            yield Label("width (ns crowbar / µs emfi)  START:END:STEP:")
            yield Input(value=self.state.width, id="width")
            yield Label("power  (crowbar 1=LP / 2=HP, EMFI ignored)  START:END:STEP:")
            yield Input(value=self.state.power, id="power")
            yield Label("settle-ms (0..60000):")
            yield Input(value=str(self.state.settle_ms), id="settle_ms")
            yield Static("", id="status_line")
            with Horizontal():
                yield Button("Configure", id="configure", variant="primary")
                yield Button("Start", id="start", variant="success")
                yield Button("Stop", id="stop", variant="error")
                yield Button("Drain", id="drain")
                yield Button("Close", id="close")

    def _sync_state_from_inputs(self) -> bool:
        try:
            engine = self.query_one("#engine", Select).value
            delay  = self.query_one("#delay", Input).value or ""
            width  = self.query_one("#width", Input).value or ""
            power  = self.query_one("#power", Input).value or ""
            settle = int(self.query_one("#settle_ms", Input).value or "0")
        except (ValueError, KeyError):
            self._set_status("error: invalid integer in form")
            return False
        candidate = CampaignFormState(
            engine=engine if isinstance(engine, str) else "crowbar",
            delay=delay, width=width, power=power, settle_ms=settle,
        )
        try:
            candidate.validate()
        except ValueError as e:
            self._set_status(f"error: {e}")
            return False
        self.state = candidate
        return True

    def _set_status(self, msg: str) -> None:
        try:
            self.query_one("#status_line", Static).update(msg)
        except Exception:
            pass

    def on_button_pressed(self, event: Button.Pressed) -> None:
        action = event.button.id or ""
        if action == "close":
            self.action_close()
            return
        if action == "configure":
            if not self._sync_state_from_inputs():
                return
            if self.configure_cb is None:
                return
            try:
                self.configure_cb(self.state)
                self._set_status("OK configured")
            except Exception as e:
                self._set_status(f"configure: {e}")
        elif action == "start" and self.start_cb:
            try:
                self.start_cb()
                self._set_status("OK started")
            except Exception as e:
                self._set_status(f"start: {e}")
        elif action == "stop" and self.stop_cb:
            try:
                self.stop_cb()
                self._set_status("OK stopped")
            except Exception as e:
                self._set_status(f"stop: {e}")
        elif action == "drain" and self.drain_cb:
            try:
                count = self.drain_cb()
                self._set_status(f"OK drain ({count} results pushed to dashboard)")
            except Exception as e:
                self._set_status(f"drain: {e}")

    def action_close(self) -> None:
        self.dismiss(None)


# -----------------------------------------------------------------
# Crowbar form state + control modal (F11-0b)
# -----------------------------------------------------------------


_CROWBAR_TRIGGERS = ("immediate", "ext_rising", "ext_falling", "ext_pulse_pos")
_CROWBAR_OUTPUTS = ("lp", "hp")     # NONE excluded — form must pick a real path


@dataclass
class CrowbarFormState:
    trigger: str = "immediate"
    output:  str = "lp"
    delay_us: int = 0
    width_ns: int = 200

    @classmethod
    def from_dict(cls, d: dict) -> CrowbarFormState:
        known = {f.name for f in fields(cls)}
        kwargs = {k: v for k, v in d.items() if k in known}
        return cls(**kwargs)

    def to_dict(self) -> dict:
        return asdict(self)

    def validate(self) -> None:
        if self.trigger not in _CROWBAR_TRIGGERS:
            raise ValueError(
                f"trigger must be one of {_CROWBAR_TRIGGERS}, got {self.trigger!r}"
            )
        if self.output not in _CROWBAR_OUTPUTS:
            raise ValueError(
                f"output must be one of {_CROWBAR_OUTPUTS}, got {self.output!r}"
            )
        if not 8 <= self.width_ns <= 50000:
            raise ValueError(
                f"width_ns out of range 8..50000 ns (driver-bounded): {self.width_ns}"
            )
        if self.delay_us < 0:
            raise ValueError(f"delay_us must be >= 0, got {self.delay_us}")


class CrowbarControlModal(ModalScreen[None]):
    """Crowbar configure / arm / fire / disarm.

    Unlike EMFI, no action involves the HV cap — the crowbar gates
    pre-existing rails through GP16 (LP, logic-level) or GP17 (HP,
    N-MOSFET). No HV confirm modal is interposed; `requires_hv_confirm`
    is False for every action."""

    DEFAULT_CSS = """
    CrowbarControlModal > Vertical {
        background: $panel;
        border: thick $accent;
        padding: 1 2;
        width: 80;
        height: auto;
    }
    CrowbarControlModal Input { width: 100%; }
    CrowbarControlModal Select { width: 100%; }
    """

    BINDINGS = [
        Binding("escape", "close", "close"),
    ]

    def __init__(
        self,
        *,
        initial: CrowbarFormState | None = None,
        apply_cb=None,
        arm_cb=None,
        fire_cb=None,
        disarm_cb=None,
    ) -> None:
        super().__init__()
        self.state = initial or CrowbarFormState()
        self.apply_cb = apply_cb
        self.arm_cb = arm_cb
        self.fire_cb = fire_cb
        self.disarm_cb = disarm_cb

    @staticmethod
    def requires_hv_confirm(action: str) -> bool:
        return False

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label("Crowbar control")
            yield Label("[dim]CDC1 · F5 crowbar_proto[/dim]")
            yield Label("trigger:")
            yield Select(
                [(t, t) for t in _CROWBAR_TRIGGERS],
                value=self.state.trigger,
                id="trigger",
            )
            yield Label("output:")
            yield Select(
                [(o.upper(), o) for o in _CROWBAR_OUTPUTS],
                value=self.state.output,
                id="output",
            )
            yield Label("delay-us:")
            yield Input(value=str(self.state.delay_us), id="delay_us")
            yield Label("width-ns (8..50000):")
            yield Input(value=str(self.state.width_ns), id="width_ns")
            yield Static("", id="status_line")
            with Horizontal():
                yield Button("Apply", id="apply", variant="primary")
                yield Button("Arm", id="arm", variant="warning")
                yield Button("Fire", id="fire", variant="success")
                yield Button("Disarm", id="disarm")
                yield Button("Close", id="close")

    def _sync_state_from_inputs(self) -> bool:
        try:
            trig = self.query_one("#trigger", Select).value
            out  = self.query_one("#output", Select).value
            d = int(self.query_one("#delay_us", Input).value or "0")
            w = int(self.query_one("#width_ns", Input).value or "0")
        except (ValueError, KeyError):
            self._set_status("error: invalid integer in form")
            return False
        candidate = CrowbarFormState(
            trigger=trig if isinstance(trig, str) else "immediate",
            output=out if isinstance(out, str) else "lp",
            delay_us=d, width_ns=w,
        )
        try:
            candidate.validate()
        except ValueError as e:
            self._set_status(f"error: {e}")
            return False
        self.state = candidate
        return True

    def _set_status(self, msg: str) -> None:
        try:
            self.query_one("#status_line", Static).update(msg)
        except Exception:
            pass

    def on_button_pressed(self, event: Button.Pressed) -> None:
        action = event.button.id or ""
        if action == "close":
            self.action_close()
            return
        if action in ("apply", "arm"):
            if not self._sync_state_from_inputs():
                return
        if action == "apply" and self.apply_cb:
            try:
                self.apply_cb(self.state)
                self._set_status("OK applied")
            except Exception as e:
                self._set_status(f"apply: {e}")
        elif action == "arm" and self.arm_cb:
            try:
                self.arm_cb(self.state)
                self._set_status("OK arm")
            except Exception as e:
                self._set_status(f"arm: {e}")
        elif action == "fire" and self.fire_cb:
            try:
                self.fire_cb()
                self._set_status("OK fire")
            except Exception as e:
                self._set_status(f"fire: {e}")
        elif action == "disarm" and self.disarm_cb:
            try:
                self.disarm_cb()
                self._set_status("OK disarm")
            except Exception as e:
                self._set_status(f"disarm: {e}")

    def action_close(self) -> None:
        self.dismiss(None)

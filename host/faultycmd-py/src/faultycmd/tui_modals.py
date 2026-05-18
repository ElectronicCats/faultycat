"""F11-0a — Textual control modals.

The dashboard `FaultycmdTUI` opens these via hotkey:

    e → EmfiControlModal       (CDC0)
    b → CrowbarControlModal    (CDC1, F11-0b)
    p → CampaignControlModal   (CDC1, F11-0c)
    n → ScannerControlModal    (CDC2, F11-0d)

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
        # `q` dismisses-as-No so the operator can q-q out of the
        # app even when this HV confirm is on top.
        Binding("q", "decide(False)", "no"),
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
        # `q` also closes the modal so the operator can press q-q
        # to fully quit the app (first q dismisses any open modal,
        # second q reaches App.action_quit). Without this, modals
        # swallow `q` and the dashboard feels frozen.
        Binding("q", "close", "close"),
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
        # `q` also closes the modal so the operator can press q-q
        # to fully quit the app (first q dismisses any open modal,
        # second q reaches App.action_quit). Without this, modals
        # swallow `q` and the dashboard feels frozen.
        Binding("q", "close", "close"),
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
        # `q` also closes the modal so the operator can press q-q
        # to fully quit the app (first q dismisses any open modal,
        # second q reaches App.action_quit). Without this, modals
        # swallow `q` and the dashboard feels frozen.
        Binding("q", "close", "close"),
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


# -----------------------------------------------------------------
# Post-scan "init with detected pins?" confirm (F11-0d follow-up)
# -----------------------------------------------------------------


class SwdInitFromScanModal(ModalScreen[tuple]):
    """Pops up after a successful ``scan swd`` to ask the operator
    whether to immediately run ``swd init`` with the detected
    SWCLK / SWDIO. The scanner does NOT probe NRST, so this modal
    exposes an editable NRST input (default ``0``; leave blank for
    no NRST) that the operator can override before pressing
    Aceptar.

    Dismiss payload is ``(accept: bool, nrst: int | None)``:
      * Aceptar → ``(True, nrst_from_input)``  (nrst may be None
        if the operator cleared the field)
      * Cerrar / Escape → ``(False, None)``"""

    DEFAULT_CSS = """
    SwdInitFromScanModal > Vertical {
        background: $panel;
        border: thick $accent;
        padding: 1 2;
        width: 70;
        height: auto;
        align: center middle;
    }
    SwdInitFromScanModal Input { width: 100%; }
    SwdInitFromScanModal Button { margin: 1 1 0 1; }
    """

    BINDINGS = [
        Binding("escape", "cancel", "close"),
        # `q` dismisses-as-cancel so the operator can q-q out of
        # the app even when this prompt is on top.
        Binding("q", "cancel", "close"),
    ]

    def __init__(
        self,
        *,
        swclk: int,
        swdio: int,
        default_nrst: int | None = 0,
    ) -> None:
        super().__init__()
        self.swclk = swclk
        self.swdio = swdio
        self.default_nrst = default_nrst

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label(
                f"SWD detected: [bold]SWCLK=GP{self.swclk}[/bold], "
                f"[bold]SWDIO=GP{self.swdio}[/bold]"
            )
            yield Label(
                "[dim]NRST was not auto-detected; defaulting to GP0 — "
                "edit or clear to suit your wiring.[/dim]"
            )
            yield Label("NRST pin (blank for none):")
            yield Input(
                value="" if self.default_nrst is None else str(self.default_nrst),
                id="nrst",
            )
            yield Static("", id="nrst_status")
            yield Label("Initialize SWD with these pins?")
            with Horizontal():
                yield Button("Aceptar", id="accept", variant="primary")
                yield Button("Cerrar",  id="close")

    def _parse_nrst(self) -> tuple[bool, int | None]:
        """Parse the NRST input. Returns ``(ok, value)`` — value is
        ``None`` when the field is blank (= no NRST). On parse
        failure shows an inline error and returns ``(False, None)``."""
        raw = self.query_one("#nrst", Input).value.strip()
        if raw == "":
            return True, None
        try:
            v = int(raw, 0)
        except ValueError:
            self.query_one("#nrst_status", Static).update(
                "error: NRST must be an integer or blank"
            )
            return False, None
        if v < 0:
            self.query_one("#nrst_status", Static).update(
                "error: NRST must be >= 0"
            )
            return False, None
        return True, v

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "accept":
            ok, nrst = self._parse_nrst()
            if not ok:
                return
            self.dismiss((True, nrst))
        elif event.button.id == "close":
            self.dismiss((False, None))

    def action_cancel(self) -> None:
        self.dismiss((False, None))


# -----------------------------------------------------------------
# Scanner / SWD control modal (F11-0d)
# -----------------------------------------------------------------


# SWCLK/SWDIO defaults mirror the firmware `BOARD_GP_*_DEFAULT`
# symbols in `drivers/include/board_v2.h`: SWCLK=GP0, SWDIO=GP1.
# NRST defaults to 0 per operator preference — the field is still
# editable; passing 0 wires NRST to GP0 (same as SWCLK), so the
# operator is expected to override before pressing Aceptar when
# the target's NRST sits on a different pin.
_SCANNER_DEFAULT_SWCLK = 0
_SCANNER_DEFAULT_SWDIO = 1
_SCANNER_DEFAULT_NRST  = 0
_SCANNER_DEFAULT_FREQ_KHZ = 1000


@dataclass
class ScannerFormState:
    swclk: int = _SCANNER_DEFAULT_SWCLK
    swdio: int = _SCANNER_DEFAULT_SWDIO
    nrst:  int | None = _SCANNER_DEFAULT_NRST
    freq_khz:    int = _SCANNER_DEFAULT_FREQ_KHZ
    read_addr:   str = "0xE000ED00"      # SCB CPUID — sanity-check default
    write_addr:  str = "0x20000000"
    write_value: str = "0x00000000"

    @classmethod
    def from_dict(cls, d: dict) -> ScannerFormState:
        known = {f.name for f in fields(cls)}
        kwargs = {k: v for k, v in d.items() if k in known}
        return cls(**kwargs)

    def to_dict(self) -> dict:
        return asdict(self)


class ScannerControlModal(ModalScreen[None]):
    """SWD pin assignment + init / deinit / freq / idcode / connect /
    read32 / write32 / reset + scan-swd, all over CDC2's text shell.

    UX is a two-step wizard so the operator never has to look at
    inputs that don't apply to the action they want to fire:

        1. Page ``menu``: just the list of action buttons + Close.
        2. Page ``<action>``: only the inputs that action needs
           (or a brief "no parameters" notice) plus an ``Aceptar``
           button that dispatches the corresponding callback and a
           ``Atrás`` button that returns to the menu.

    The dashboard wires each callback through ``_run_scanner_task``,
    which temporarily drops the diag tail's hold on CDC2 so the
    scanner shell can own the port for the duration of the call and
    then reinstates the diag tail. Results land in ``#status_line``
    of this modal (persisted across page switches)."""

    DEFAULT_CSS = """
    ScannerControlModal > Vertical {
        background: $panel;
        border: thick $accent;
        padding: 1 2;
        width: 110;
        height: auto;
    }
    ScannerControlModal Input { width: 100%; }
    ScannerControlModal Button { margin: 0 1; }
    /* 5×2 menu: two Horizontal rows of 5 buttons each. `1fr`
       distributes each row's width evenly; `min-width: 0` cancels
       Button's intrinsic minimum so long labels don't force an
       uneven layout. */
    ScannerControlModal .menu_row {
        height: 3;
        width: 100%;
        margin-bottom: 1;
    }
    ScannerControlModal .menu_row Button {
        width: 1fr;
        height: 3;
        margin: 0 1;
        min-width: 0;
    }
    """

    BINDINGS = [
        Binding("escape", "close", "close"),
        # `q` also closes the modal so the operator can press q-q
        # to fully quit the app (first q dismisses any open modal,
        # second q reaches App.action_quit). Without this, modals
        # swallow `q` and the dashboard feels frozen.
        Binding("q", "close", "close"),
    ]

    # All page container ids. The first one is the menu; the rest
    # are per-action input pages. `_show_page(action)` flips display
    # on these via `id == target`.
    _PAGE_IDS = (
        "menu",
        "page_init",
        "page_deinit",
        "page_freq",
        "page_idcode",
        "page_connect",
        "page_read32",
        "page_write32",
        "page_reset",
        "page_scan_swd",
    )

    def __init__(
        self,
        *,
        initial: ScannerFormState | None = None,
        init_cb=None,
        deinit_cb=None,
        freq_cb=None,
        idcode_cb=None,
        connect_cb=None,
        read32_cb=None,
        write32_cb=None,
        reset_cb=None,
        scan_swd_cb=None,
    ) -> None:
        super().__init__()
        self.state = initial or ScannerFormState()
        self.init_cb = init_cb
        self.deinit_cb = deinit_cb
        self.freq_cb = freq_cb
        self.idcode_cb = idcode_cb
        self.connect_cb = connect_cb
        self.read32_cb = read32_cb
        self.write32_cb = write32_cb
        self.reset_cb = reset_cb
        self.scan_swd_cb = scan_swd_cb
        self.current_page: str = "menu"

    def compose(self) -> ComposeResult:
        with Vertical():
            yield Label("Scanner / SWD control")
            yield Label("[dim]CDC2 · F6 swd shell · F8-2 scan[/dim]")

            # -- page: menu (action picker) --------------------
            # 10 actions laid out as 5 cols × 2 rows so the menu
            # reads horizontally first instead of taking 10
            # vertical rows. Implemented as two Horizontals because
            # Textual 8.x's Grid kept collapsing to the natural
            # cell width when the modal's parent Vertical didn't
            # propagate the outer `width: 110`.
            with Vertical(id="menu"):
                yield Label("Choose an action:")
                with Horizontal(classes="menu_row"):
                    yield Button("Init",     id="menu_init",     variant="primary")
                    yield Button("Deinit",   id="menu_deinit")
                    yield Button("Freq",     id="menu_freq")
                    yield Button("IDCODE",   id="menu_idcode",   variant="success")
                    yield Button("Connect",  id="menu_connect")
                with Horizontal(classes="menu_row"):
                    yield Button("Read32",   id="menu_read32")
                    yield Button("Write32",  id="menu_write32")
                    yield Button("Reset",    id="menu_reset",    variant="warning")
                    yield Button("Scan SWD", id="menu_scan_swd", variant="primary")
                    yield Button("Close",    id="menu_close")

            # -- page: init ------------------------------------
            with Vertical(id="page_init"):
                yield Label("[bold]swd init[/bold] — pin assignment")
                yield Label("SWCLK:")
                yield Input(value=str(self.state.swclk), id="swclk")
                yield Label("SWDIO:")
                yield Input(value=str(self.state.swdio), id="swdio")
                yield Label("NRST (blank for none):")
                yield Input(
                    value="" if self.state.nrst is None else str(self.state.nrst),
                    id="nrst",
                )
                with Horizontal():
                    yield Button("Aceptar", id="apply_init", variant="primary")
                    yield Button("Atrás",   id="back_init")

            # -- page: freq ------------------------------------
            with Vertical(id="page_freq"):
                yield Label("[bold]swd freq[/bold] — set SWCLK rate")
                yield Label("freq-khz:")
                yield Input(value=str(self.state.freq_khz), id="freq_khz")
                with Horizontal():
                    yield Button("Aceptar", id="apply_freq", variant="primary")
                    yield Button("Atrás",   id="back_freq")

            # -- page: read32 ----------------------------------
            with Vertical(id="page_read32"):
                yield Label("[bold]swd read32[/bold] — single-word read")
                yield Label("addr (hex, e.g. 0xE000ED00):")
                yield Input(value=self.state.read_addr, id="read_addr")
                with Horizontal():
                    yield Button("Aceptar", id="apply_read32", variant="primary")
                    yield Button("Atrás",   id="back_read32")

            # -- page: write32 ---------------------------------
            with Vertical(id="page_write32"):
                yield Label("[bold]swd write32[/bold] — single-word write")
                yield Label("addr (hex):")
                yield Input(value=self.state.write_addr,  id="write_addr")
                yield Label("value (hex):")
                yield Input(value=self.state.write_value, id="write_value")
                with Horizontal():
                    yield Button("Aceptar", id="apply_write32", variant="primary")
                    yield Button("Atrás",   id="back_write32")

            # -- pages with no params --------------------------
            with Vertical(id="page_deinit"):
                yield Label("[bold]swd deinit[/bold] — release the SWD pins")
                yield Label("[dim]No parameters.[/dim]")
                with Horizontal():
                    yield Button("Aceptar", id="apply_deinit", variant="primary")
                    yield Button("Atrás",   id="back_deinit")
            with Vertical(id="page_idcode"):
                yield Label("[bold]swd idcode[/bold] — detect bus, request IDCODE/DPIDR")
                yield Label("[dim]No parameters.[/dim]")
                with Horizontal():
                    yield Button("Aceptar", id="apply_idcode", variant="primary")
                    yield Button("Atrás",   id="back_idcode")
            with Vertical(id="page_connect"):
                yield Label("[bold]swd connect[/bold] — firmware TARGETSEL path")
                yield Label("[dim]No parameters.[/dim]")
                with Horizontal():
                    yield Button("Aceptar", id="apply_connect", variant="primary")
                    yield Button("Atrás",   id="back_connect")
            with Vertical(id="page_reset"):
                yield Label("[bold]swd reset[/bold] — NRST pulse: assert → 50 ms → deassert")
                yield Label("[dim]No parameters.[/dim]")
                with Horizontal():
                    yield Button("Aceptar", id="apply_reset", variant="primary")
                    yield Button("Atrás",   id="back_reset")
            with Vertical(id="page_scan_swd"):
                yield Label("[bold]scan swd[/bold] — bus-wide discovery (timeout 30 s)")
                yield Label("[dim]No parameters.[/dim]")
                with Horizontal():
                    yield Button("Aceptar", id="apply_scan_swd", variant="primary")
                    yield Button("Atrás",   id="back_scan_swd")

            # Status line persists across page switches.
            yield Static("", id="status_line")

    def on_mount(self) -> None:
        # Start on the menu; hide every input page until selected.
        self._show_page("menu")

    # -- page navigation -------------------------------------------

    def _show_page(self, page: str) -> None:
        """``page`` is one of the entries in ``_PAGE_IDS`` minus
        the ``page_`` prefix, or the literal ``"menu"``."""
        target = "menu" if page == "menu" else f"page_{page}"
        if target not in self._PAGE_IDS:
            return
        self.current_page = page
        for pid in self._PAGE_IDS:
            try:
                self.query_one(f"#{pid}").display = (pid == target)
            except Exception:
                # During unit-construction (no DOM) `query_one`
                # raises; safe to ignore — `on_mount` re-runs the
                # show once the widgets are real.
                pass

    # -- input sync helpers ----------------------------------------

    def _sync_pins(self) -> bool:
        try:
            swclk = int(self.query_one("#swclk", Input).value or "0")
            swdio = int(self.query_one("#swdio", Input).value or "0")
            nrst_str = self.query_one("#nrst", Input).value.strip()
            nrst: int | None = int(nrst_str) if nrst_str != "" else None
        except ValueError:
            self._set_status("error: invalid integer")
            return False
        if swclk < 0 or swdio < 0 or (nrst is not None and nrst < 0):
            self._set_status("error: pins must be >= 0")
            return False
        self.state.swclk, self.state.swdio, self.state.nrst = swclk, swdio, nrst
        return True

    def _sync_freq(self) -> bool:
        try:
            khz = int(self.query_one("#freq_khz", Input).value or "0")
        except ValueError:
            self._set_status("error: invalid frequency")
            return False
        if khz <= 0:
            self._set_status("error: freq must be > 0")
            return False
        self.state.freq_khz = khz
        return True

    def _set_status(self, msg: str) -> None:
        try:
            self.query_one("#status_line", Static).update(msg)
        except Exception:
            pass

    # -- button dispatch -------------------------------------------

    def on_button_pressed(self, event: Button.Pressed) -> None:
        bid = event.button.id or ""

        # --- Menu buttons: navigate to per-action page.
        if bid.startswith("menu_"):
            sub = bid[len("menu_"):]
            if sub == "close":
                self.action_close()
                return
            self._set_status("")
            self._show_page(sub)
            return

        # --- Back buttons on any page: return to menu.
        if bid.startswith("back_"):
            self._set_status("")
            self._show_page("menu")
            return

        # --- Apply buttons: dispatch the matching callback.
        if bid == "apply_init":
            if not self._sync_pins():
                return
            if self.init_cb is None:
                return
            try:
                self.init_cb(self.state.swclk, self.state.swdio, self.state.nrst)
                self._set_status("init dispatched…")
            except Exception as e:
                self._set_status(f"init: {e}")
        elif bid == "apply_deinit":
            if self.deinit_cb is None:
                return
            try:
                self.deinit_cb()
                self._set_status("deinit dispatched…")
            except Exception as e:
                self._set_status(f"deinit: {e}")
        elif bid == "apply_freq":
            if not self._sync_freq():
                return
            if self.freq_cb is None:
                return
            try:
                self.freq_cb(self.state.freq_khz)
                self._set_status("freq dispatched…")
            except Exception as e:
                self._set_status(f"freq: {e}")
        elif bid == "apply_idcode":
            if self.idcode_cb is None:
                return
            try:
                self.idcode_cb()
                self._set_status("idcode dispatched…")
            except Exception as e:
                self._set_status(f"idcode: {e}")
        elif bid == "apply_connect":
            if self.connect_cb is None:
                return
            try:
                self.connect_cb()
                self._set_status("connect dispatched…")
            except Exception as e:
                self._set_status(f"connect: {e}")
        elif bid == "apply_read32":
            try:
                addr = int(self.query_one("#read_addr", Input).value or "0", 0)
            except ValueError:
                self._set_status("error: read addr not hex/int")
                return
            self.state.read_addr = f"0x{addr:08X}"
            if self.read32_cb is None:
                return
            try:
                self.read32_cb(addr)
                self._set_status("read32 dispatched…")
            except Exception as e:
                self._set_status(f"read32: {e}")
        elif bid == "apply_write32":
            try:
                addr  = int(self.query_one("#write_addr",  Input).value or "0", 0)
                value = int(self.query_one("#write_value", Input).value or "0", 0)
            except ValueError:
                self._set_status("error: write addr/value not hex/int")
                return
            self.state.write_addr  = f"0x{addr:08X}"
            self.state.write_value = f"0x{value:08X}"
            if self.write32_cb is None:
                return
            try:
                self.write32_cb(addr, value)
                self._set_status("write32 dispatched…")
            except Exception as e:
                self._set_status(f"write32: {e}")
        elif bid == "apply_reset":
            if self.reset_cb is None:
                return
            try:
                self.reset_cb()
                self._set_status("reset pulse dispatched…")
            except Exception as e:
                self._set_status(f"reset: {e}")
        elif bid == "apply_scan_swd":
            if self.scan_swd_cb is None:
                return
            try:
                self.scan_swd_cb()
                self._set_status("scan-swd dispatched…")
            except Exception as e:
                self._set_status(f"scan-swd: {e}")

    def action_close(self) -> None:
        self.dismiss(None)

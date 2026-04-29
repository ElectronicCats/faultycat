"""F11-0a — EMFI control modal + HV confirm modal state tests.

Same constraint as `test_tui.py`: we deliberately avoid Textual's
Pilot test framework (incompat with our daemon-thread / asyncio
shutdown setup, see `fix(F10-polish)`). These tests inspect data
state, not rendered output. Real-device smoke against a live
FaultyCat covers the visual side.
"""
from __future__ import annotations

import pytest

from faultycmd.tui_modals import (
    CrowbarControlModal,
    CrowbarFormState,
    EmfiControlModal,
    EmfiFormState,
    HvConfirmModal,
)

# -- EmfiFormState (the dataclass that drives the form) -----------

def test_emfi_form_state_defaults():
    s = EmfiFormState()
    assert s.trigger == "immediate"
    assert s.delay_us == 0
    assert s.width_us == 5
    assert s.charge_timeout_ms == 0


def test_emfi_form_state_from_dict_partial():
    """Last-config blobs may be partial (older revs / hand-edits).
    Missing fields fall back to defaults."""
    s = EmfiFormState.from_dict({"width_us": 10, "trigger": "ext_rising"})
    assert s.trigger == "ext_rising"
    assert s.width_us == 10
    assert s.delay_us == 0
    assert s.charge_timeout_ms == 0


def test_emfi_form_state_from_dict_ignores_unknown_keys():
    s = EmfiFormState.from_dict({"width_us": 7, "xxx": "garbage"})
    assert s.width_us == 7


def test_emfi_form_state_to_dict_roundtrip():
    s = EmfiFormState(trigger="ext_pulse_pos", delay_us=1200, width_us=10,
                      charge_timeout_ms=500)
    d = s.to_dict()
    s2 = EmfiFormState.from_dict(d)
    assert s == s2


def test_emfi_form_state_validates_width_bounds():
    """Driver bound: 1..50 µs. Out-of-range raises ValueError so
    the modal can show a toast instead of sending bad config."""
    with pytest.raises(ValueError):
        EmfiFormState(width_us=0).validate()
    with pytest.raises(ValueError):
        EmfiFormState(width_us=51).validate()
    EmfiFormState(width_us=1).validate()
    EmfiFormState(width_us=50).validate()


def test_emfi_form_state_validates_trigger_enum():
    valid = ("immediate", "ext_rising", "ext_falling", "ext_pulse_pos")
    for t in valid:
        EmfiFormState(trigger=t).validate()
    with pytest.raises(ValueError):
        EmfiFormState(trigger="bogus").validate()


# -- EmfiControlModal construction --------------------------------

def test_emfi_modal_construction_with_no_last_config():
    modal = EmfiControlModal(initial=EmfiFormState())
    assert modal.state.trigger == "immediate"
    assert modal.state.width_us == 5


def test_emfi_modal_construction_prefills_from_last_config():
    initial = EmfiFormState.from_dict({
        "trigger": "ext_rising",
        "delay_us": 1500,
        "width_us": 8,
    })
    modal = EmfiControlModal(initial=initial)
    assert modal.state.trigger == "ext_rising"
    assert modal.state.delay_us == 1500
    assert modal.state.width_us == 8


# -- Action gating: Arm requires HV confirm, Fire does not --------

def test_emfi_modal_arm_requires_hv_confirm():
    """The intent: pressing 'Arm' should NOT directly invoke the
    EMFI client. It schedules the action behind an HV confirm
    modal. This is the safety gate the F10-polish smoke flagged
    as missing — accidental focus traversal could otherwise arm
    the HV cap."""
    modal = EmfiControlModal(initial=EmfiFormState())
    assert modal.requires_hv_confirm("arm") is True


def test_emfi_modal_fire_does_not_require_hv_confirm():
    """Fire is gated by being already armed (firmware-side); the
    operator already passed the HV confirm at arm time. A second
    confirm at fire time is friction without safety value."""
    modal = EmfiControlModal(initial=EmfiFormState())
    assert modal.requires_hv_confirm("fire") is False


def test_emfi_modal_disarm_does_not_require_hv_confirm():
    """Disarm is the *safe* direction — never gate it."""
    modal = EmfiControlModal(initial=EmfiFormState())
    assert modal.requires_hv_confirm("disarm") is False


def test_emfi_modal_apply_does_not_require_hv_confirm():
    """Apply config doesn't touch HV state."""
    modal = EmfiControlModal(initial=EmfiFormState())
    assert modal.requires_hv_confirm("apply") is False


# -- HvConfirmModal -----------------------------------------------

def test_hv_confirm_modal_carries_action_label():
    m = HvConfirmModal(action_label="Arm EMFI")
    assert m.action_label == "Arm EMFI"


def test_hv_confirm_modal_default_decision_is_no():
    """Default focus must NOT be on the destructive choice. The
    operator must explicitly hit Yes; pressing Enter without
    moving focus is a No."""
    m = HvConfirmModal(action_label="Arm EMFI")
    assert m.default_decision is False


# -- form ↔ wire-protocol mapping ---------------------------------

def test_emfi_form_trigger_strings_all_map_to_emfi_trigger_enum():
    """Regression: every trigger string the form exposes must
    round-trip into the wire-level `EmfiTrigger` IntEnum the same
    way the CLI maps it (`EmfiTrigger[trigger.upper()]`). When this
    mapping breaks, `EmfiClient.configure` does `int(trigger)` over
    a raw string and crashes with `ValueError: invalid literal for
    int() with base 10: 'immediate'` — exactly the smoke regression
    that triggered the F11-0a-fix commit."""
    from faultycmd.protocols.emfi import EmfiTrigger
    from faultycmd.tui_modals import _EMFI_TRIGGERS

    for trig_str in _EMFI_TRIGGERS:
        # Must not raise — every UI string is a valid enum name.
        enum_val = EmfiTrigger[trig_str.upper()]
        # And int() of the enum is what configure() puts on the wire.
        assert isinstance(int(enum_val), int)


# -- CrowbarFormState ---------------------------------------------

def test_crowbar_form_state_defaults():
    s = CrowbarFormState()
    assert s.trigger == "immediate"
    assert s.output == "lp"
    assert s.delay_us == 0
    assert s.width_ns == 200


def test_crowbar_form_state_from_dict_partial():
    s = CrowbarFormState.from_dict({"output": "hp", "width_ns": 1000})
    assert s.output == "hp"
    assert s.width_ns == 1000
    assert s.trigger == "immediate"  # default
    assert s.delay_us == 0


def test_crowbar_form_state_from_dict_ignores_unknown():
    s = CrowbarFormState.from_dict({"width_ns": 100, "garbage": "x"})
    assert s.width_ns == 100


def test_crowbar_form_state_to_dict_roundtrip():
    s = CrowbarFormState(trigger="ext_rising", output="hp",
                         delay_us=2500, width_ns=500)
    s2 = CrowbarFormState.from_dict(s.to_dict())
    assert s == s2


def test_crowbar_form_state_validates_width_bounds():
    """Driver bound: 8..50000 ns."""
    with pytest.raises(ValueError):
        CrowbarFormState(width_ns=7).validate()
    with pytest.raises(ValueError):
        CrowbarFormState(width_ns=50001).validate()
    CrowbarFormState(width_ns=8).validate()
    CrowbarFormState(width_ns=50000).validate()


def test_crowbar_form_state_validates_trigger_enum():
    for t in ("immediate", "ext_rising", "ext_falling", "ext_pulse_pos"):
        CrowbarFormState(trigger=t).validate()
    with pytest.raises(ValueError):
        CrowbarFormState(trigger="bogus").validate()


def test_crowbar_form_state_validates_output_enum():
    CrowbarFormState(output="lp").validate()
    CrowbarFormState(output="hp").validate()
    with pytest.raises(ValueError):
        # NONE is a wire-level enum but the form must never select it.
        CrowbarFormState(output="none").validate()
    with pytest.raises(ValueError):
        CrowbarFormState(output="zzz").validate()


# -- CrowbarControlModal ------------------------------------------

def test_crowbar_modal_construction_defaults():
    m = CrowbarControlModal(initial=CrowbarFormState())
    assert m.state.trigger == "immediate"
    assert m.state.output == "lp"


def test_crowbar_modal_construction_prefills():
    m = CrowbarControlModal(
        initial=CrowbarFormState.from_dict({"output": "hp", "width_ns": 1500}),
    )
    assert m.state.output == "hp"
    assert m.state.width_ns == 1500


def test_crowbar_modal_no_action_requires_hv_confirm():
    """Crowbar does NOT involve the HV cap (it gates either GP16
    LP path or GP17 N-MOSFET on already-present rails). Unlike
    EMFI, no action requires the HV confirm modal."""
    m = CrowbarControlModal(initial=CrowbarFormState())
    for action in ("apply", "arm", "fire", "disarm", "capture"):
        assert m.requires_hv_confirm(action) is False


# -- form ↔ wire-protocol drift guards (mirror EMFI guards) -------

def test_crowbar_form_strings_all_map_to_wire_enums():
    """Mirror of `test_emfi_form_trigger_strings_all_map...`. Every
    UI string for trigger AND output must round-trip through the
    `CrowbarTrigger[s.upper()]` / `CrowbarOutput[s.upper()]` map
    that the wire-level configure() expects."""
    from faultycmd.protocols.crowbar import CrowbarOutput, CrowbarTrigger
    from faultycmd.tui_modals import _CROWBAR_OUTPUTS, _CROWBAR_TRIGGERS

    for trig_str in _CROWBAR_TRIGGERS:
        assert isinstance(int(CrowbarTrigger[trig_str.upper()]), int)
    for out_str in _CROWBAR_OUTPUTS:
        assert isinstance(int(CrowbarOutput[out_str.upper()]), int)


def test_crowbar_client_method_signatures_unchanged():
    """Mirror of `test_emfi_client_method_signatures_unchanged`.
    Pins the kwarg surface that the modal closures call."""
    import inspect

    from faultycmd.protocols.crowbar import CrowbarClient

    sigs = {
        "configure": {"trigger", "output", "delay_us", "width_ns"},
        "arm":       set(),
        "fire":      {"trigger_timeout_ms"},
        "disarm":    set(),
        "status":    set(),
        "ping":      set(),
    }
    for method, expected in sigs.items():
        sig = inspect.signature(getattr(CrowbarClient, method))
        params = {p for p in sig.parameters if p != "self"}
        assert expected.issubset(params), (
            f"CrowbarClient.{method} lost kwargs: "
            f"expected {expected}, got {params}"
        )


def test_emfi_client_method_signatures_unchanged():
    """Regression: the modal's action closures call `EmfiClient`
    with specific keyword names (configure / arm / fire / disarm /
    capture). When the protocol-side signature drifts (a kwarg
    rename, a new required arg) the modal silently breaks at smoke
    time. This test pins the surface so any drift fails CI before
    smoke."""
    import inspect

    from faultycmd.protocols.emfi import EmfiClient

    sigs = {
        "configure": {"trigger", "delay_us", "width_us", "charge_timeout_ms"},
        "arm":       set(),
        "fire":      {"trigger_timeout_ms"},
        "disarm":    set(),
        "capture":   {"offset", "length"},
        "status":    set(),
        "ping":      set(),
    }
    for method, expected_kwargs in sigs.items():
        sig = inspect.signature(getattr(EmfiClient, method))
        # Drop `self`.
        params = {p for p in sig.parameters if p != "self"}
        assert expected_kwargs.issubset(params), (
            f"EmfiClient.{method} lost kwargs: "
            f"expected {expected_kwargs}, got {params}"
        )

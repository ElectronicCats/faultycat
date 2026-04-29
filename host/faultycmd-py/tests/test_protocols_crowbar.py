"""Unit tests for faultycmd.protocols.crowbar."""
from __future__ import annotations

import struct

import pytest

from faultycmd.protocols import CrowbarClient, EngineError
from faultycmd.protocols.crowbar import (
    CMD_CONFIGURE,
    CMD_FIRE,
    CMD_PING,
    CMD_STATUS,
    CrowbarErr,
    CrowbarOutput,
    CrowbarState,
    CrowbarTrigger,
)
from tests.conftest import FakeSerial, make_fake_factory


def _client(fake: FakeSerial) -> CrowbarClient:
    return CrowbarClient("/dev/null", serial_factory=make_fake_factory(fake))


def test_ping_round_trip():
    fake = FakeSerial()
    fake.queue_reply(CMD_PING, b"F5\x00\x00")
    with _client(fake) as cli:
        assert cli.ping() == b"F5\x00\x00"


def test_configure_packs_payload():
    fake = FakeSerial()
    fake.queue_reply(CMD_CONFIGURE, bytes([CrowbarErr.NONE]))
    with _client(fake) as cli:
        cli.configure(
            trigger=CrowbarTrigger.IMMEDIATE,
            output=CrowbarOutput.HP,
            delay_us=2000,
            width_ns=300,
        )
    frames = fake.take_written_frames()
    payload = frames[0][4:-2]
    assert payload[0] == CrowbarTrigger.IMMEDIATE
    assert payload[1] == CrowbarOutput.HP
    assert struct.unpack("<II", payload[2:10]) == (2000, 300)


def test_configure_path_not_selected_raises():
    fake = FakeSerial()
    fake.queue_reply(CMD_CONFIGURE, bytes([CrowbarErr.PATH_NOT_SELECTED]))
    with _client(fake) as cli, pytest.raises(EngineError) as ei:
        cli.configure(CrowbarTrigger.IMMEDIATE, CrowbarOutput.NONE, 0, 100)
    assert ei.value.code == CrowbarErr.PATH_NOT_SELECTED


def test_fire_packs_timeout():
    fake = FakeSerial()
    fake.queue_reply(CMD_FIRE, bytes([CrowbarErr.NONE]))
    with _client(fake) as cli:
        cli.fire(trigger_timeout_ms=500)
    frames = fake.take_written_frames()
    payload = frames[0][4:-2]
    assert struct.unpack("<I", payload)[0] == 500


def test_status_decodes_full_struct():
    payload = bytes([CrowbarState.FIRED, CrowbarErr.NONE]) + struct.pack(
        "<III", 99999, 250, 2000
    ) + bytes([CrowbarOutput.HP])
    fake = FakeSerial()
    fake.queue_reply(CMD_STATUS, payload)
    with _client(fake) as cli:
        st = cli.status()
    assert st.state == CrowbarState.FIRED
    assert st.last_fire_at_ms == 99999
    assert st.pulse_width_ns_actual == 250
    assert st.delay_us_actual == 2000
    assert st.output == CrowbarOutput.HP


def test_status_short_reply_raises():
    fake = FakeSerial()
    fake.queue_reply(CMD_STATUS, b"\x00")
    with _client(fake) as cli, pytest.raises(EngineError):
        cli.status()

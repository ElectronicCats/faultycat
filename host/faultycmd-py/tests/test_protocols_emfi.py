"""Unit tests for faultycmd.protocols.emfi."""
from __future__ import annotations

import struct

import pytest

from faultycmd.protocols import EngineError, EmfiClient
from faultycmd.protocols.emfi import (
    CMD_CAPTURE,
    CMD_CONFIGURE,
    CMD_DISARM,
    CMD_FIRE,
    CMD_PING,
    CMD_STATUS,
    EmfiErr,
    EmfiState,
    EmfiTrigger,
)
from tests.conftest import FakeSerial, make_fake_factory


def _client(fake: FakeSerial) -> EmfiClient:
    return EmfiClient("/dev/null", serial_factory=make_fake_factory(fake))


# -- ping ---------------------------------------------------------

def test_ping_round_trip():
    fake = FakeSerial()
    fake.queue_reply(CMD_PING, b"F4\x00\x00")
    with _client(fake) as cli:
        payload = cli.ping()
    assert payload == b"F4\x00\x00"

    frames = fake.take_written_frames()
    assert len(frames) == 1
    assert frames[0][1] == CMD_PING


# -- configure ----------------------------------------------------

def test_configure_packs_payload():
    fake = FakeSerial()
    fake.queue_reply(CMD_CONFIGURE, bytes([EmfiErr.NONE]))
    with _client(fake) as cli:
        cli.configure(
            trigger=EmfiTrigger.IMMEDIATE,
            delay_us=1234,
            width_us=5,
            charge_timeout_ms=500,
        )
    frames = fake.take_written_frames()
    payload = frames[0][4:-2]   # strip SOF/CMD/LEN and CRC
    assert payload[0] == int(EmfiTrigger.IMMEDIATE)
    assert struct.unpack("<III", payload[1:13]) == (1234, 5, 500)


def test_configure_engine_error_raises():
    fake = FakeSerial()
    fake.queue_reply(CMD_CONFIGURE, bytes([EmfiErr.BAD_CONFIG]))
    with _client(fake) as cli, pytest.raises(EngineError) as ei:
        cli.configure(EmfiTrigger.IMMEDIATE, 0, 1)
    assert ei.value.code == EmfiErr.BAD_CONFIG


# -- arm / fire / disarm -----------------------------------------

def test_arm_ok():
    fake = FakeSerial()
    fake.queue_reply(0x11, bytes([EmfiErr.NONE]))
    with _client(fake) as cli:
        cli.arm()


def test_arm_engine_error():
    fake = FakeSerial()
    fake.queue_reply(0x11, bytes([EmfiErr.HV_NOT_CHARGED]))
    with _client(fake) as cli, pytest.raises(EngineError) as ei:
        cli.arm()
    assert ei.value.code == EmfiErr.HV_NOT_CHARGED


def test_fire_packs_timeout():
    fake = FakeSerial()
    fake.queue_reply(CMD_FIRE, bytes([EmfiErr.NONE]))
    with _client(fake) as cli:
        cli.fire(trigger_timeout_ms=2500)
    frames = fake.take_written_frames()
    payload = frames[0][4:-2]
    assert struct.unpack("<I", payload)[0] == 2500


def test_disarm_always_returns():
    # Firmware always replies err=NONE; disarm() doesn't raise.
    fake = FakeSerial()
    fake.queue_reply(CMD_DISARM, bytes([EmfiErr.NONE]))
    with _client(fake) as cli:
        cli.disarm()


# -- status -------------------------------------------------------

def test_status_decodes_struct():
    payload = bytes([EmfiState.FIRED, EmfiErr.NONE]) + struct.pack(
        "<IIII", 12345, 8192, 5, 1000
    )
    fake = FakeSerial()
    fake.queue_reply(CMD_STATUS, payload)
    with _client(fake) as cli:
        st = cli.status()
    assert st.state == EmfiState.FIRED
    assert st.err == EmfiErr.NONE
    assert st.last_fire_at_ms == 12345
    assert st.capture_fill == 8192
    assert st.pulse_width_us_actual == 5
    assert st.delay_us_actual == 1000


def test_status_short_reply_raises():
    fake = FakeSerial()
    fake.queue_reply(CMD_STATUS, b"\x00\x00")
    with _client(fake) as cli, pytest.raises(EngineError):
        cli.status()


# -- capture ------------------------------------------------------

def test_capture_returns_payload():
    body = bytes(range(64))
    fake = FakeSerial()
    fake.queue_reply(CMD_CAPTURE, body)
    with _client(fake) as cli:
        got = cli.capture(offset=0, length=64)
    assert got == body
    frames = fake.take_written_frames()
    payload = frames[0][4:-2]
    assert struct.unpack("<HH", payload) == (0, 64)


def test_capture_engine_error_byte():
    fake = FakeSerial()
    fake.queue_reply(CMD_CAPTURE, bytes([EmfiErr.BAD_CONFIG]))
    with _client(fake) as cli, pytest.raises(EngineError) as ei:
        cli.capture(offset=0, length=64)
    assert ei.value.code == EmfiErr.BAD_CONFIG


def test_capture_rejects_too_long():
    fake = FakeSerial()
    with _client(fake) as cli, pytest.raises(ValueError):
        cli.capture(offset=0, length=600)


def test_capture_rejects_overflow_offset():
    fake = FakeSerial()
    with _client(fake) as cli, pytest.raises(ValueError):
        cli.capture(offset=8000, length=512)


# -- lifecycle ----------------------------------------------------

def test_must_open_before_send():
    fake = FakeSerial()
    cli = _client(fake)
    with pytest.raises(RuntimeError):
        cli.ping()


def test_context_manager_closes():
    fake = FakeSerial()
    fake.queue_reply(CMD_PING, b"F4\x00\x00")
    with _client(fake) as cli:
        cli.ping()
    assert fake.closed

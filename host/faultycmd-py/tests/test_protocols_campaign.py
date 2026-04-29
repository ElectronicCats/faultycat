"""Unit tests for faultycmd.protocols.campaign."""
from __future__ import annotations

import struct

import pytest

from faultycmd.protocols import CampaignClient
from faultycmd.protocols.campaign import (
    CMD_CONFIG,
    CMD_DRAIN,
    CMD_START,
    CMD_STATUS,
    CMD_STOP,
    DRAIN_MAX_COUNT,
    RECORD_LEN,
    STATUS_REPLY_LEN,
    CampaignError,
    CampaignErr,
    CampaignState,
    ProtoStatus,
)
from tests.conftest import FakeSerial, make_fake_factory


def _client(fake: FakeSerial, engine: str = "crowbar") -> CampaignClient:
    return CampaignClient(
        "/dev/null", engine=engine, serial_factory=make_fake_factory(fake)
    )


# -- configure ----------------------------------------------------

def test_configure_packs_40_byte_payload():
    fake = FakeSerial()
    fake.queue_reply(CMD_CONFIG, bytes([ProtoStatus.OK]))
    with _client(fake) as cli:
        cli.configure(
            delay=(1000, 3000, 1000),
            width=(200, 300, 100),
            power=(1, 1, 0),
            settle_ms=50,
        )
    frames = fake.take_written_frames()
    payload = frames[0][4:-2]
    assert len(payload) == 40
    fields = struct.unpack("<10I", payload)
    assert fields == (1000, 3000, 1000, 200, 300, 100, 1, 1, 0, 50)


def test_configure_rejected_raises():
    fake = FakeSerial()
    fake.queue_reply(CMD_CONFIG, bytes([ProtoStatus.ERR_REJECTED]))
    with _client(fake) as cli, pytest.raises(CampaignError) as ei:
        cli.configure((100, 50, 1), (1, 1, 0), (0, 0, 0))   # inverted
    assert ei.value.status_code == ProtoStatus.ERR_REJECTED


# -- start / stop --------------------------------------------------

def test_start_ok():
    fake = FakeSerial()
    fake.queue_reply(CMD_START, bytes([ProtoStatus.OK]))
    with _client(fake) as cli:
        cli.start()


def test_start_rejected():
    fake = FakeSerial()
    fake.queue_reply(CMD_START, bytes([ProtoStatus.ERR_REJECTED]))
    with _client(fake) as cli, pytest.raises(CampaignError):
        cli.start()


def test_stop_always_ok():
    fake = FakeSerial()
    fake.queue_reply(CMD_STOP, bytes([ProtoStatus.OK]))
    with _client(fake) as cli:
        cli.stop()


# -- status --------------------------------------------------------

def _status_payload(
    state: int = CampaignState.SWEEPING,
    err: int = CampaignErr.NONE,
    step_n: int = 0, total: int = 6,
    pushed: int = 0, dropped: int = 0,
) -> bytes:
    return (
        bytes([state, err, 0, 0])
        + struct.pack("<4I", step_n, total, pushed, dropped)
    )


def test_status_decodes():
    fake = FakeSerial()
    fake.queue_reply(CMD_STATUS, _status_payload(
        state=CampaignState.SWEEPING, step_n=2, total=6, pushed=2,
    ))
    with _client(fake) as cli:
        st = cli.status()
    assert st.state == CampaignState.SWEEPING
    assert st.step_n == 2
    assert st.total_steps == 6
    assert st.results_pushed == 2


def test_status_short_reply_raises():
    fake = FakeSerial()
    fake.queue_reply(CMD_STATUS, b"\x00\x00\x00")
    with _client(fake) as cli, pytest.raises(CampaignError):
        cli.status()


# -- drain ---------------------------------------------------------

def _record_bytes(step_n: int, delay: int, width: int, power: int,
                  fire: int = 0, verify: int = 0,
                  target: int = 0, ts_us: int = 0) -> bytes:
    return (
        struct.pack("<4I", step_n, delay, width, power)
        + bytes([fire, verify, 0, 0])
        + struct.pack("<II", target, ts_us)
    )


def test_drain_empty_returns_empty_list():
    fake = FakeSerial()
    fake.queue_reply(CMD_DRAIN, bytes([0]))
    with _client(fake) as cli:
        assert cli.drain() == []


def test_drain_decodes_records():
    rec0 = _record_bytes(0, 1000, 200, 1, fire=0, verify=0, target=1, ts_us=12345)
    rec1 = _record_bytes(1, 1000, 300, 1, fire=0, verify=0, target=1, ts_us=12500)
    fake = FakeSerial()
    fake.queue_reply(CMD_DRAIN, bytes([2]) + rec0 + rec1)
    with _client(fake) as cli:
        results = cli.drain()
    assert len(results) == 2
    assert results[0].step_n == 0
    assert results[0].delay == 1000
    assert results[0].width == 200
    assert results[0].ts_us == 12345
    assert results[1].step_n == 1
    assert results[1].width == 300


def test_drain_clamps_max_count():
    fake = FakeSerial()
    fake.queue_reply(CMD_DRAIN, bytes([0]))
    with _client(fake) as cli:
        cli.drain(max_count=100)   # request 100 → clamps to 18
    frames = fake.take_written_frames()
    payload = frames[0][4:-2]
    assert payload[0] == DRAIN_MAX_COUNT


def test_drain_floor_clamps_to_one():
    fake = FakeSerial()
    fake.queue_reply(CMD_DRAIN, bytes([0]))
    with _client(fake) as cli:
        cli.drain(max_count=0)
    frames = fake.take_written_frames()
    assert frames[0][4:-2][0] == 1


def test_drain_all_iterates_until_short():
    rec = _record_bytes(0, 1000, 200, 1)
    fake = FakeSerial()
    # First call: ring full at DRAIN_MAX_COUNT records.
    fake.queue_reply(CMD_DRAIN, bytes([DRAIN_MAX_COUNT]) + rec * DRAIN_MAX_COUNT)
    # Second call: ring partly drained.
    fake.queue_reply(CMD_DRAIN, bytes([3]) + rec * 3)
    with _client(fake) as cli:
        all_results = list(cli.drain_all())
    assert len(all_results) == DRAIN_MAX_COUNT + 3


# -- engine field carried on construction --------------------------

def test_engine_field_propagates():
    fake = FakeSerial()
    cli = _client(fake, engine="emfi")
    assert cli.engine == "emfi"


def test_constants_match_mutex_internals_spec():
    # Sanity check — the F9-4 wire format is frozen per
    # docs/MUTEX_INTERNALS.md §4.
    assert STATUS_REPLY_LEN == 20
    assert RECORD_LEN == 28
    assert DRAIN_MAX_COUNT == 18

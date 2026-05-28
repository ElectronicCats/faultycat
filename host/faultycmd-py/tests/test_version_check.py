"""Unit tests for faultycmd.version_check."""

from __future__ import annotations

import pytest

from faultycmd import __version__
from faultycmd.version_check import (
    VersionMismatchError,
    allow_mismatch,
    assert_version_match,
    host_version_tuple,
    parse_ping_version,
    parse_shell_version,
    set_allow_mismatch,
)


@pytest.fixture(autouse=True)
def _reset_global():
    # Tests must never leak the override into one another.
    set_allow_mismatch(False)
    yield
    set_allow_mismatch(False)


# -- host_version_tuple -------------------------------------------


def test_host_version_tuple_parses_self():
    tup = host_version_tuple()
    assert len(tup) == 4
    assert all(isinstance(v, int) for v in tup)
    assert ".".join(str(v) for v in tup) == __version__


# -- parse_ping_version -------------------------------------------


def test_parse_ping_version_unpacks_six_byte_payload():
    payload = bytes([ord("F"), ord("4"), 3, 1, 4, 1])
    assert parse_ping_version(payload) == (3, 1, 4, 1)


def test_parse_ping_version_handles_crowbar_family():
    payload = bytes([ord("F"), ord("5"), 12, 0, 0, 0])
    assert parse_ping_version(payload) == (12, 0, 0, 0)


def test_parse_ping_version_legacy_four_byte_reply_raises_pre_versioning():
    # Pre-F11 firmware replied with 4 bytes — the host must flag this
    # explicitly rather than silently treat it as 0.0.0.0.
    legacy = b"F4\x00\x00"
    with pytest.raises(VersionMismatchError) as ei:
        parse_ping_version(legacy)
    assert ei.value.firmware is None
    assert "predates" in str(ei.value)


def test_parse_ping_version_malformed_raises():
    with pytest.raises(VersionMismatchError):
        parse_ping_version(b"")
    with pytest.raises(VersionMismatchError):
        parse_ping_version(b"X4\x00\x00\x00\x00")


# -- parse_shell_version ------------------------------------------


def test_parse_shell_version_strips_prefix():
    assert parse_shell_version("SHELL: VERSION 3.1.4.1") == (3, 1, 4, 1)
    assert parse_shell_version("SHELL:   VERSION   12.0.0.0  ") == (12, 0, 0, 0)


def test_parse_shell_version_malformed_raises():
    with pytest.raises(VersionMismatchError):
        parse_shell_version("SHELL: HELP")
    with pytest.raises(VersionMismatchError):
        parse_shell_version("SHELL: VERSION 3.0")


# -- assert_version_match -----------------------------------------


def test_assert_version_match_passes_on_exact():
    # Whatever the live host version is, parse it and assert it matches.
    assert_version_match(host_version_tuple())


def test_assert_version_match_raises_on_any_segment_difference():
    fw = host_version_tuple()
    # Bump TWEAK to force a mismatch; Exact policy means *any* segment
    # difference triggers.
    bumped = (fw[0], fw[1], fw[2], fw[3] + 1)
    with pytest.raises(VersionMismatchError):
        assert_version_match(bumped)


def test_assert_version_match_honours_global_override():
    fw = host_version_tuple()
    bumped = (fw[0] + 1, fw[1], fw[2], fw[3])
    set_allow_mismatch(True)
    assert allow_mismatch() is True
    # Should NOT raise.
    assert_version_match(bumped)

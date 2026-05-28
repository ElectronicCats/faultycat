"""Firmware ↔ host version parity check.

The firmware embeds its 4-segment version (`MAJOR.MINOR.PATCH.TWEAK`)
in two places the host already talks to:

* PING reply on CDC0/CDC1 (emfi/crowbar proto): bytes `'F', family,
  MAJOR, MINOR, PATCH, TWEAK` (6 B payload, up from the legacy 4 B).
* `version` text command on CDC2 (scanner shell): single line
  `SHELL: VERSION MAJOR.MINOR.PATCH.TWEAK`.

Each client validates on connect that the firmware version matches
`faultycmd.__version__` byte-for-byte (Exact policy). Mismatches abort
the connection with a clear "re-flash the matching UF2" message.
Operators can override the check globally with the CLI flag
``--ignore-version-mismatch`` — that flips
:func:`set_allow_mismatch`, which the per-client probes consult.

The version files in the repo (``/VERSION``, ``__init__.py``,
``pyproject.toml``, CMake ``project(VERSION ...)``) are kept in sync
by the tag-driven GitHub release workflow.
"""

from __future__ import annotations

import re

from . import __version__

VersionTuple = tuple[int, int, int, int]

_HOST_VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)$")
_SHELL_VERSION_RE = re.compile(r"^SHELL:\s+VERSION\s+(\d+)\.(\d+)\.(\d+)\.(\d+)\s*$")


# Module-level flag toggled by `faultycmd --ignore-version-mismatch`.
# Off by default so production CLI/TUI fail-closed on mismatch.
_allow_mismatch = False


def set_allow_mismatch(allow: bool) -> None:
    """Toggle the global override that bypasses the version check.

    Wired to the CLI's top-level ``--ignore-version-mismatch`` flag.
    Leaving this False (the default) is the safe production behaviour.
    """
    global _allow_mismatch
    _allow_mismatch = allow


def allow_mismatch() -> bool:
    return _allow_mismatch


class VersionMismatchError(RuntimeError):
    """Raised when the firmware version does not match the host's.

    Attributes:
        firmware: the 4-segment version the firmware reported, or
            ``None`` if the firmware predates the version embed
            (legacy 4-byte PING reply, shell with no `version` verb).
        host: the host's `faultycmd.__version__` at check time.
    """

    def __init__(
        self,
        firmware: VersionTuple | None,
        host: str,
        *,
        hint: str = "",
    ) -> None:
        self.firmware = firmware
        self.host = host
        fw_str = ".".join(str(v) for v in firmware) if firmware else "<pre-versioning>"
        msg = (
            f"firmware/host version mismatch: firmware={fw_str}, host={host}. "
            "Re-flash the matching UF2 from the GitHub Release, or pass "
            "--ignore-version-mismatch to bypass (unsafe — wire protocol "
            "may have shifted)."
        )
        if hint:
            msg += f" [{hint}]"
        super().__init__(msg)


def host_version_tuple() -> VersionTuple:
    """Parse ``faultycmd.__version__`` into a 4-int tuple."""
    m = _HOST_VERSION_RE.match(__version__)
    if not m:
        raise RuntimeError(
            f"faultycmd.__version__ is malformed: {__version__!r} "
            "(expected `MAJOR.MINOR.PATCH.TWEAK`)"
        )
    return int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))


def parse_ping_version(payload: bytes) -> VersionTuple:
    """Parse the PING reply payload into a version tuple.

    Layout (F11+ firmware): ``'F', family, MAJ, MIN, PATCH, TWEAK``.
    The legacy 4-byte reply (`'F', family, 0, 0`) trips the length
    check and surfaces as VersionMismatchError(firmware=None) so the
    host shows a meaningful "firmware too old" message instead of a
    cryptic 0.0.0.0 mismatch.
    """
    if len(payload) < 2 or payload[0:1] != b"F":
        raise VersionMismatchError(
            None, __version__, hint=f"unexpected ping reply: {payload!r}"
        )
    if len(payload) < 6:
        raise VersionMismatchError(
            None, __version__, hint="firmware predates the version-in-PING extension"
        )
    return payload[2], payload[3], payload[4], payload[5]


def parse_shell_version(line: str) -> VersionTuple:
    """Parse the CDC2 `version` reply (``SHELL: VERSION X.Y.Z.W``)."""
    m = _SHELL_VERSION_RE.match(line.strip())
    if not m:
        raise VersionMismatchError(
            None, __version__, hint=f"bad shell version reply: {line!r}"
        )
    return int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4))


def assert_version_match(firmware: VersionTuple) -> None:
    """Exact-match firmware against the host's ``__version__``.

    No-op if the global override is on (see :func:`set_allow_mismatch`).
    """
    if _allow_mismatch:
        return
    host = host_version_tuple()
    if firmware != host:
        raise VersionMismatchError(firmware, __version__)

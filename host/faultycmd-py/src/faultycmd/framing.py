"""CRC16-CCITT framing primitives shared across all faultycmd
binary host protocols (F4 emfi_proto, F5 crowbar_proto, F9-4
campaign_proto, and the F8-4 / F8-5 binary modes).

Wire format::

    [SOF=0xFA] [CMD] [LEN_LO] [LEN_HI] [PAYLOAD..LEN] [CRC_LO] [CRC_HI]

The CRC covers the four-byte header from CMD through the end of the
payload (i.e. *not* including the leading SOF byte). Polynomial is
the CCITT 0x1021 with initial value 0xFFFF — matches what the
firmware computes in `services/host_proto/{emfi,crowbar,campaign}_
proto/<name>_proto.c::write_frame`.

The parser is intentionally simple: on any byte that isn't a SOF
mid-stream, it skips and resyncs at the next SOF. CRC mismatches
raise :class:`FrameCRCError` rather than silently dropping — the
caller sees the failure and can recover.
"""
from __future__ import annotations

import struct
import time
from typing import Protocol

SOF = 0xFA
HEADER_LEN = 3   # CMD + LEN_LO + LEN_HI
CRC_LEN = 2
FRAME_OVERHEAD = 1 + HEADER_LEN + CRC_LEN   # SOF + header + CRC = 6


class _ByteReader(Protocol):
    """Minimal interface :func:`read_frame` needs.

    `serial.Serial` from pyserial satisfies this; tests mock it with
    a fake reader that returns scripted bytes.
    """

    def read(self, size: int = 1) -> bytes: ...


class FrameError(Exception):
    """Base class for frame-parsing errors."""


class FrameTimeout(FrameError):  # noqa: N818 — public API; kept short for ergonomics
    """No complete frame arrived within the read timeout."""


class FrameCRCError(FrameError):
    """A frame arrived with a mismatched CRC. Caller may retry."""


def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT, polynomial 0x1021, initial value 0xFFFF.

    Matches `crc16_ccitt` in the firmware's
    `services/host_proto/<name>_proto/<name>_proto.c`.
    """
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    """Wrap (cmd, payload) into the wire frame shape.

    Args:
        cmd: 1-byte command or reply (replies use cmd | 0x80 in the
            firmware; the caller decides which side it is).
        payload: 0..N bytes; LEN field is u16 little-endian, so up to
            65535 in theory but each proto has its own MAX_PAYLOAD
            ceiling (typically 512).

    Returns:
        Bytes ready to write to a serial port.
    """
    if not 0 <= cmd <= 0xFF:
        raise ValueError(f"cmd byte out of range: 0x{cmd:X}")
    body = bytes([cmd]) + struct.pack("<H", len(payload)) + payload
    crc = crc16_ccitt(body)
    return bytes([SOF]) + body + struct.pack("<H", crc)


def read_frame(reader: _ByteReader, timeout: float = 2.0) -> tuple[int, bytes]:
    """Read one frame from `reader`.

    Args:
        reader: anything with `.read(size)` returning bytes — `serial.Serial`
            from pyserial works directly.
        timeout: total wallclock seconds to spend waiting. Per-byte
            reads use the underlying reader's timeout (typically much
            shorter), and we loop.

    Returns:
        Tuple of (cmd, payload) on success.

    Raises:
        FrameTimeout: no SOF / no complete frame arrived in time.
        FrameCRCError: a frame arrived but CRC didn't validate.
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = reader.read(1)
        if not b:
            continue
        if b[0] != SOF:
            # Resync — skip noise / mid-frame stragglers.
            continue
        header = reader.read(HEADER_LEN)
        if len(header) != HEADER_LEN:
            continue
        cmd = header[0]
        length = struct.unpack("<H", header[1:3])[0]
        payload = reader.read(length) if length else b""
        crc_bytes = reader.read(CRC_LEN)
        if len(payload) != length or len(crc_bytes) != CRC_LEN:
            continue
        expected = struct.unpack("<H", crc_bytes)[0]
        calc = crc16_ccitt(header + payload)
        if expected != calc:
            raise FrameCRCError(
                f"CRC mismatch on cmd=0x{cmd:02X}: "
                f"expected 0x{expected:04X}, calc 0x{calc:04X}"
            )
        return cmd, payload
    raise FrameTimeout(f"no frame received within {timeout:.1f}s")

"""Shared base for the per-CDC binary protocol clients.

Each subclass wraps a single CDC + a fixed opcode table. The base
handles:

  - Lifecycle: :class:`BinaryProtoClient` is a context manager
    opening a :class:`serial.Serial` on entry, closing it on exit.
    Tests construct with ``port=None`` and inject a fake serial
    via the ``serial_factory`` hook.
  - Frame round-trip: :meth:`_send` writes the request frame and
    reads the reply, validating the reply CMD matches
    ``request_cmd | 0x80`` (the firmware's reply convention from
    F4 onwards).
"""

from __future__ import annotations

from collections.abc import Callable
from typing import Protocol

from ..framing import build_frame, read_frame


class _SerialLike(Protocol):
    """Subset of ``serial.Serial`` :class:`BinaryProtoClient` uses."""

    def write(self, data: bytes) -> int: ...
    def read(self, size: int = 1) -> bytes: ...
    def reset_input_buffer(self) -> None: ...
    def close(self) -> None: ...


class ProtocolError(Exception):
    """Wire-level error: unexpected reply CMD, frame parse failure
    after retries, etc."""


class EngineError(Exception):
    """Engine-level error: the firmware reported a non-zero error
    code in a 1-byte status reply (e.g. EMFI_ERR_HV_NOT_CHARGED).

    Attributes:
        code: the raw error byte from the firmware reply.
    """

    def __init__(self, code: int, label: str = "") -> None:
        self.code = code
        super().__init__(label or f"engine error 0x{code:02X}")


SerialFactory = Callable[[str, int, float], _SerialLike]


def _default_serial_factory(port: str, baud: int, per_byte_timeout: float) -> _SerialLike:
    """Real :func:`serial.Serial` factory. Imports pyserial lazily so
    tests that inject a fake factory don't need pyserial installed."""
    import serial  # noqa: PLC0415 — lazy to keep the test surface tiny

    return serial.Serial(port, baud, timeout=per_byte_timeout)


class BinaryProtoClient:
    """Base class for emfi / crowbar / campaign clients.

    Subclasses define the opcode constants and high-level methods
    (``ping``, ``configure``, etc.). The base owns the serial port
    lifecycle and the request/reply round-trip.
    """

    DEFAULT_BAUD = 115200
    DEFAULT_TIMEOUT = 2.0
    PER_BYTE_TIMEOUT = 0.5

    # PING opcode that returns the firmware version. Both emfi_proto
    # and crowbar_proto reply to 0x01 with the 6-byte version payload;
    # CampaignClient pings via this same opcode (the firmware
    # multiplexes by CMD on the shared CDC, so the request still hits
    # whichever of {emfi,crowbar}_proto owns the CDC and returns the
    # firmware's version).
    _VERSION_CHECK_CMD = 0x01

    def __init__(
        self,
        port: str,
        *,
        baud: int = DEFAULT_BAUD,
        timeout: float = DEFAULT_TIMEOUT,
        serial_factory: SerialFactory | None = None,
        check_firmware_version: bool = True,
    ) -> None:
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._factory = serial_factory or _default_serial_factory
        self._ser: _SerialLike | None = None
        self._check_firmware_version = check_firmware_version
        # Populated on open() once the firmware PINGs back, or left as
        # None if the connection was opened with check_firmware_version
        # = False (tests, --ignore-version-mismatch dev path).
        self.firmware_version: tuple[int, int, int, int] | None = None

    # -- lifecycle ----------------------------------------------------

    def open(self) -> None:
        if self._ser is not None:
            return
        self._ser = self._factory(self.port, self.baud, self.PER_BYTE_TIMEOUT)
        if self._check_firmware_version:
            self._probe_and_check_firmware_version()

    def _probe_and_check_firmware_version(self) -> None:
        """Send PING and validate the version against the host's.

        Raises VersionMismatchError on mismatch (unless the global
        override is on). Closes the serial port on failure so the
        caller doesn't end up holding a half-open client.
        """
        from ..version_check import (  # noqa: PLC0415 — avoid import cycle
            assert_version_match,
            parse_ping_version,
        )

        try:
            payload = self._send(self._VERSION_CHECK_CMD)
            self.firmware_version = parse_ping_version(payload)
            assert_version_match(self.firmware_version)
        except Exception:
            # A half-open client is worse than no client.
            if self._ser is not None:
                self._ser.close()
                self._ser = None
            raise

    def close(self) -> None:
        if self._ser is None:
            return
        self._ser.close()
        self._ser = None

    def __enter__(self) -> BinaryProtoClient:
        self.open()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    # -- frame round-trip ---------------------------------------------

    def _require_serial(self) -> _SerialLike:
        if self._ser is None:
            raise RuntimeError("client not open — use it as a context manager or call open() first")
        return self._ser

    def _send(self, cmd: int, payload: bytes = b"") -> bytes:
        """Send a request frame and return the reply payload bytes.

        Raises:
            ProtocolError: reply CMD mismatch.
            FrameTimeout / FrameCRCError: from :func:`framing.read_frame`.
        """
        ser = self._require_serial()
        ser.reset_input_buffer()
        ser.write(build_frame(cmd, payload))
        reply_cmd, reply_payload = read_frame(ser, timeout=self.timeout)
        expected = cmd | 0x80
        if reply_cmd != expected:
            raise ProtocolError(
                f"reply CMD mismatch: expected 0x{expected:02X}, "
                f"got 0x{reply_cmd:02X} (request was 0x{cmd:02X})"
            )
        return reply_payload

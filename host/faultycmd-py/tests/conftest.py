"""Shared test fixtures — FakeSerial + frame builders for reply
scripts that match what the firmware would emit.
"""
from __future__ import annotations

from collections import deque
from typing import Iterable

from faultycmd.framing import build_frame


class FakeSerial:
    """Minimal stand-in for :class:`serial.Serial` used by the
    protocol-client tests.

    Bytes written by the client land in :attr:`written`. Bytes
    returned to the client come from :attr:`tx_queue` (the test's
    "outgoing-from-firmware" script).
    """

    def __init__(self, replies: Iterable[bytes] = ()) -> None:
        self.written: bytearray = bytearray()
        self.tx_queue: bytearray = bytearray()
        for r in replies:
            self.tx_queue.extend(r)
        self.closed = False
        self.reset_calls = 0

    # -- API the client exercises ----------------------------------

    def write(self, data: bytes) -> int:
        self.written.extend(data)
        return len(data)

    def read(self, size: int = 1) -> bytes:
        if not self.tx_queue:
            return b""
        chunk = bytes(self.tx_queue[:size])
        del self.tx_queue[:size]
        return chunk

    def reset_input_buffer(self) -> None:
        # NOTE: real serial.Serial.reset_input_buffer drops bytes
        # already in the OS RX buffer — it does NOT (and CAN'T) drop
        # bytes the firmware hasn't sent yet. Our tx_queue models the
        # latter, so we leave it alone. We only count the call so a
        # test can assert reset_input_buffer fired before each request.
        self.reset_calls += 1

    def close(self) -> None:
        self.closed = True

    # -- helpers for tests ----------------------------------------

    def queue_reply(self, cmd: int, payload: bytes = b"") -> None:
        """Queue a frame *as the firmware would emit it* — request CMD
        with the high bit set."""
        self.tx_queue.extend(build_frame(cmd | 0x80, payload))

    def take_written_frames(self) -> list[bytes]:
        """Split self.written into individual frames based on SOF (0xFA)
        + LEN. Useful for asserting the client's wire output."""
        frames: list[bytes] = []
        buf = bytes(self.written)
        i = 0
        while i < len(buf):
            if buf[i] != 0xFA:
                i += 1
                continue
            if i + 4 > len(buf):
                break
            length = buf[i + 2] | (buf[i + 3] << 8)
            end = i + 6 + length
            if end > len(buf):
                break
            frames.append(buf[i:end])
            i = end
        return frames


def make_fake_factory(fake: FakeSerial):
    """Returns a serial_factory callable that always hands back the
    same :class:`FakeSerial` instance regardless of port/baud."""
    def factory(port: str, baud: int, per_byte_timeout: float):
        return fake
    return factory

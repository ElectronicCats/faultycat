"""F4 emfi_proto client (CDC0).

Wire layout per ``services/host_proto/emfi_proto/emfi_proto.h``::

    PING (0x01)        in: -                       out: 4 B "F4\\0\\0"
    CONFIGURE (0x10)   in: 1 trigger + 3×u32 LE    out: 1 B err
    ARM (0x11)         in: -                       out: 1 B err
    FIRE (0x12)        in: u32 LE timeout_ms       out: 1 B err
    DISARM (0x13)      in: -                       out: 1 B err
    STATUS (0x14)      in: -                       out: 18 B status
    CAPTURE (0x15)     in: 2×u16 LE off+len        out: <len> B
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

from ..usb import cdc_for
from ._base import BinaryProtoClient, EngineError

# -- opcodes (mirror emfi_proto.h) ------------------------------------

CMD_PING      = 0x01
CMD_CONFIGURE = 0x10
CMD_ARM       = 0x11
CMD_FIRE      = 0x12
CMD_DISARM    = 0x13
CMD_STATUS    = 0x14
CMD_CAPTURE   = 0x15


class EmfiTrigger(IntEnum):
    """``emfi_trig_t`` from services/glitch_engine/emfi/emfi_pio.h."""

    IMMEDIATE     = 0
    EXT_RISING    = 1
    EXT_FALLING   = 2
    EXT_PULSE_POS = 3


class EmfiState(IntEnum):
    """``emfi_state_t`` from services/glitch_engine/emfi/emfi_campaign.h."""

    IDLE    = 0
    ARMING  = 1
    CHARGED = 2
    WAITING = 3
    FIRED   = 4
    ERROR   = 5


class EmfiErr(IntEnum):
    """``emfi_err_t``."""

    NONE             = 0
    BAD_CONFIG       = 1
    HV_NOT_CHARGED   = 2
    TRIGGER_TIMEOUT  = 3
    PIO_FAULT        = 4
    INTERNAL         = 5


@dataclass
class EmfiStatus:
    state: EmfiState
    err: EmfiErr
    last_fire_at_ms: int
    capture_fill: int
    pulse_width_us_actual: int
    delay_us_actual: int


class EmfiClient(BinaryProtoClient):
    """F4 emfi_proto over CDC0."""

    @classmethod
    def discover(cls, **kw: object) -> "EmfiClient":
        """Construct with the default CDC0 port via :func:`usb.cdc_for`."""
        return cls(cdc_for("emfi"), **kw)   # type: ignore[arg-type]

    # -- ops ----------------------------------------------------------

    def ping(self) -> bytes:
        """Round-trip CMD_PING. Returns the 4-byte payload (b'F4\\0\\0')."""
        return self._send(CMD_PING)

    def configure(
        self,
        trigger: EmfiTrigger | int,
        delay_us: int,
        width_us: int,
        charge_timeout_ms: int = 0,
    ) -> None:
        """Configure the next fire path. Raises :class:`EngineError`
        if the firmware rejects the params (typically BAD_CONFIG)."""
        payload = bytes([int(trigger)]) + struct.pack(
            "<III", delay_us, width_us, charge_timeout_ms
        )
        self._raise_on_err(self._send(CMD_CONFIGURE, payload))

    def arm(self) -> None:
        """Transition the campaign to ARMING (HV charge starts)."""
        self._raise_on_err(self._send(CMD_ARM))

    def fire(self, trigger_timeout_ms: int = 1000) -> None:
        """Wait for trigger up to ``trigger_timeout_ms``, then PIO fires."""
        self._raise_on_err(self._send(CMD_FIRE, struct.pack("<I", trigger_timeout_ms)))

    def disarm(self) -> None:
        """Hard-disarm (drops HV PWM, resets state)."""
        self._send(CMD_DISARM)   # firmware always replies err=NONE

    def status(self) -> EmfiStatus:
        rpl = self._send(CMD_STATUS)
        if len(rpl) < 18:
            raise EngineError(0xFE, f"short STATUS reply: {len(rpl)} B")
        state, err = rpl[0], rpl[1]
        last, fill, width_actual, delay_actual = struct.unpack("<IIII", rpl[2:18])
        return EmfiStatus(
            state=EmfiState(state) if state in EmfiState._value2member_map_ else state,  # type: ignore[arg-type]
            err=EmfiErr(err) if err in EmfiErr._value2member_map_ else err,              # type: ignore[arg-type]
            last_fire_at_ms=last,
            capture_fill=fill,
            pulse_width_us_actual=width_actual,
            delay_us_actual=delay_actual,
        )

    def capture(self, offset: int = 0, length: int = 512) -> bytes:
        """Read ADC capture buffer slice."""
        if length > 512:
            raise ValueError(f"length {length} exceeds 512 B per CMD_CAPTURE frame")
        if offset + length > 8192:
            raise ValueError(f"offset+length {offset + length} exceeds 8192 B ring")
        rpl = self._send(CMD_CAPTURE, struct.pack("<HH", offset, length))
        if len(rpl) == 1:
            # Firmware returned a 1-byte error code.
            raise EngineError(rpl[0])
        return rpl

    # -- internals ----------------------------------------------------

    @staticmethod
    def _raise_on_err(reply: bytes) -> None:
        if not reply:
            raise EngineError(0xFE, "empty reply payload")
        code = reply[0]
        if code != EmfiErr.NONE:
            try:
                err = EmfiErr(code)
                raise EngineError(code, f"emfi err: {err.name}")
            except ValueError:
                raise EngineError(code) from None


__all__ = [
    "CMD_PING",
    "CMD_CONFIGURE",
    "CMD_ARM",
    "CMD_FIRE",
    "CMD_DISARM",
    "CMD_STATUS",
    "CMD_CAPTURE",
    "EmfiTrigger",
    "EmfiState",
    "EmfiErr",
    "EmfiStatus",
    "EmfiClient",
]


# Stub the unused param to keep tooling quiet while keeping the
# `Optional` import meaningful for type-checker friendliness.
_ = Optional

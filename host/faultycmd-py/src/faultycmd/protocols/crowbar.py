"""F5 crowbar_proto client (CDC1).

Wire layout per ``services/host_proto/crowbar_proto/crowbar_proto.h``::

    PING (0x01)        in: -                       out: 4 B "F5\\0\\0"
    CONFIGURE (0x10)   in: 1 trig + 1 out + 2×u32  out: 1 B err
                          (delay_us, width_ns)
    ARM (0x11)         in: -                       out: 1 B err
    FIRE (0x12)        in: u32 LE timeout_ms       out: 1 B err
    DISARM (0x13)      in: -                       out: 1 B err
    STATUS (0x14)      in: -                       out: 15 B status

CAMPAIGN_* opcodes (0x20..0x24) on this CDC are handled by
:mod:`faultycmd.protocols.campaign` — they ride alongside but use a
different module so the engine selection stays explicit.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum

from ..usb import cdc_for
from ._base import BinaryProtoClient, EngineError

CMD_PING      = 0x01
CMD_CONFIGURE = 0x10
CMD_ARM       = 0x11
CMD_FIRE      = 0x12
CMD_DISARM    = 0x13
CMD_STATUS    = 0x14


class CrowbarTrigger(IntEnum):
    """``crowbar_trig_t`` from services/glitch_engine/crowbar/crowbar_pio.h."""

    IMMEDIATE     = 0
    EXT_RISING    = 1
    EXT_FALLING   = 2
    EXT_PULSE_POS = 3


class CrowbarOutput(IntEnum):
    """``crowbar_out_t`` — selects which gate the PIO drives."""

    NONE = 0
    LP   = 1   # GP16 — low-power path
    HP   = 2   # GP17 — N-MOSFET (real voltage glitch)


class CrowbarState(IntEnum):
    """``crowbar_state_t``."""

    IDLE    = 0
    ARMING  = 1
    ARMED   = 2
    WAITING = 3
    FIRED   = 4
    ERROR   = 5


class CrowbarErr(IntEnum):
    """``crowbar_err_t``."""

    NONE              = 0
    BAD_CONFIG        = 1
    TRIGGER_TIMEOUT   = 2
    PIO_FAULT         = 3
    INTERNAL          = 4
    PATH_NOT_SELECTED = 5


@dataclass
class CrowbarStatus:
    state: CrowbarState
    err: CrowbarErr
    last_fire_at_ms: int
    pulse_width_ns_actual: int
    delay_us_actual: int
    output: CrowbarOutput


class CrowbarClient(BinaryProtoClient):
    """F5 crowbar_proto over CDC1."""

    @classmethod
    def discover(cls, **kw: object) -> CrowbarClient:
        return cls(cdc_for("crowbar"), **kw)   # type: ignore[arg-type]

    def ping(self) -> bytes:
        return self._send(CMD_PING)

    def configure(
        self,
        trigger: CrowbarTrigger | int,
        output: CrowbarOutput | int,
        delay_us: int,
        width_ns: int,
    ) -> None:
        payload = (
            bytes([int(trigger), int(output)])
            + struct.pack("<II", delay_us, width_ns)
        )
        self._raise_on_err(self._send(CMD_CONFIGURE, payload))

    def arm(self) -> None:
        self._raise_on_err(self._send(CMD_ARM))

    def fire(self, trigger_timeout_ms: int = 1000) -> None:
        self._raise_on_err(self._send(CMD_FIRE, struct.pack("<I", trigger_timeout_ms)))

    def disarm(self) -> None:
        self._send(CMD_DISARM)

    def status(self) -> CrowbarStatus:
        rpl = self._send(CMD_STATUS)
        if len(rpl) < 15:
            raise EngineError(0xFE, f"short STATUS reply: {len(rpl)} B")
        state, err = rpl[0], rpl[1]
        last, width_actual, delay_actual = struct.unpack("<III", rpl[2:14])
        output = rpl[14]
        return CrowbarStatus(
            state=CrowbarState(state) if state in CrowbarState._value2member_map_ else state,  # type: ignore[arg-type]
            err=CrowbarErr(err) if err in CrowbarErr._value2member_map_ else err,              # type: ignore[arg-type]
            last_fire_at_ms=last,
            pulse_width_ns_actual=width_actual,
            delay_us_actual=delay_actual,
            output=CrowbarOutput(output) if output in CrowbarOutput._value2member_map_ else output,  # type: ignore[arg-type]
        )

    @staticmethod
    def _raise_on_err(reply: bytes) -> None:
        if not reply:
            raise EngineError(0xFE, "empty reply payload")
        code = reply[0]
        if code != CrowbarErr.NONE:
            try:
                err = CrowbarErr(code)
                raise EngineError(code, f"crowbar err: {err.name}")
            except ValueError:
                raise EngineError(code) from None


__all__ = [
    "CMD_PING",
    "CMD_CONFIGURE",
    "CMD_ARM",
    "CMD_FIRE",
    "CMD_DISARM",
    "CMD_STATUS",
    "CrowbarTrigger",
    "CrowbarOutput",
    "CrowbarState",
    "CrowbarErr",
    "CrowbarStatus",
    "CrowbarClient",
]

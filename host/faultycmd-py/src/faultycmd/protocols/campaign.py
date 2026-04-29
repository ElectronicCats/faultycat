"""F9-4 campaign_proto client.

Multiplexed on top of F4 emfi_proto (CDC0 — EMFI campaigns) or F5
crowbar_proto (CDC1 — crowbar campaigns); the engine is implied by
which CDC the command arrives on. The wire format is shared via
``services/host_proto/campaign_proto/`` and is identical on both
CDCs.

Wire layout::

    CONFIG  (0x20)  in: 40 B (10 × u32 LE)        out: 1 B status
    START   (0x21)  in: -                         out: 1 B status
    STOP    (0x22)  in: -                         out: 1 B status (always OK)
    STATUS  (0x23)  in: -                         out: 20 B status struct
    DRAIN   (0x24)  in: 1 B max_count             out: 1 B n + n × 28 B records

CONFIG payload (10 × u32 LE):
    delay_start, delay_end, delay_step,
    width_start, width_end, width_step,
    power_start, power_end, power_step,
    settle_ms

DRAIN reply: 1 B n + n records of:
    step_n (u32), delay (u32), width (u32), power (u32),
    fire_status (u8), verify_status (u8), reserved[2],
    target_state (u32), ts_us (u32)
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass
from enum import IntEnum
from typing import Iterator, Literal

from ..usb import cdc_for
from ._base import BinaryProtoClient

CMD_CONFIG = 0x20
CMD_START  = 0x21
CMD_STOP   = 0x22
CMD_STATUS = 0x23
CMD_DRAIN  = 0x24

CONFIG_PAYLOAD_LEN  = 40
STATUS_REPLY_LEN    = 20
RECORD_LEN          = 28
DRAIN_MAX_COUNT     = 18

Engine = Literal["emfi", "crowbar"]


class CampaignState(IntEnum):
    """``campaign_state_t`` from services/campaign_manager/."""

    IDLE        = 0
    CONFIGURING = 1
    SWEEPING    = 2
    DONE        = 3
    STOPPED     = 4
    ERROR       = 5


class CampaignErr(IntEnum):
    """``campaign_err_t``."""

    NONE            = 0
    BAD_CONFIG      = 1
    NOT_CONFIGURED  = 2
    BUS_BUSY        = 3
    STEP_FAILED     = 4
    INTERNAL        = 5


class ProtoStatus(IntEnum):
    """1-byte status replies from CONFIG / START / STOP."""

    OK            = 0x00
    ERR_BAD_LEN   = 0x01
    ERR_REJECTED  = 0x02


@dataclass
class CampaignStatus:
    state: CampaignState
    err: CampaignErr
    step_n: int
    total_steps: int
    results_pushed: int
    results_dropped: int


@dataclass
class CampaignResult:
    step_n: int
    delay: int
    width: int
    power: int
    fire_status: int
    verify_status: int
    target_state: int
    ts_us: int


class CampaignError(Exception):
    """The firmware rejected a campaign command (BAD_LEN / REJECTED)."""

    def __init__(self, status_code: int, label: str = "") -> None:
        self.status_code = status_code
        super().__init__(label or f"campaign proto status 0x{status_code:02X}")


class CampaignClient(BinaryProtoClient):
    """F9-4 campaign_proto over CDC0 (EMFI) or CDC1 (crowbar)."""

    def __init__(self, port: str, *, engine: Engine, **kw: object) -> None:
        super().__init__(port, **kw)   # type: ignore[arg-type]
        self.engine = engine

    @classmethod
    def discover(cls, engine: Engine, **kw: object) -> "CampaignClient":
        return cls(cdc_for(engine), engine=engine, **kw)

    # -- ops ----------------------------------------------------------

    def configure(
        self,
        delay: tuple[int, int, int],
        width: tuple[int, int, int],
        power: tuple[int, int, int],
        settle_ms: int = 0,
    ) -> None:
        """Configure a sweep. Each axis is ``(start, end, step)``;
        ``step == 0`` collapses the axis to its start value.

        Raises:
            CampaignError: firmware rejected the config (e.g. inverted
                axis, total_steps == 0, mid-sweep reconfigure).
        """
        payload = struct.pack(
            "<10I",
            delay[0], delay[1], delay[2],
            width[0], width[1], width[2],
            power[0], power[1], power[2],
            settle_ms,
        )
        rpl = self._send(CMD_CONFIG, payload)
        self._check_proto_status(rpl)

    def start(self) -> None:
        rpl = self._send(CMD_START)
        self._check_proto_status(rpl)

    def stop(self) -> None:
        rpl = self._send(CMD_STOP)
        self._check_proto_status(rpl)

    def status(self) -> CampaignStatus:
        rpl = self._send(CMD_STATUS)
        if len(rpl) < STATUS_REPLY_LEN:
            raise CampaignError(0xFE, f"short STATUS reply: {len(rpl)} B")
        state, err = rpl[0], rpl[1]
        step_n, total, pushed, dropped = struct.unpack("<4I", rpl[4:20])
        return CampaignStatus(
            state=CampaignState(state) if state in CampaignState._value2member_map_ else state,  # type: ignore[arg-type]
            err=CampaignErr(err) if err in CampaignErr._value2member_map_ else err,              # type: ignore[arg-type]
            step_n=step_n,
            total_steps=total,
            results_pushed=pushed,
            results_dropped=dropped,
        )

    def drain(self, max_count: int = DRAIN_MAX_COUNT) -> list[CampaignResult]:
        """Pop up to ``max_count`` results from the firmware ringbuffer.

        ``max_count`` is clamped at :data:`DRAIN_MAX_COUNT` (18, fits
        in EMFI_PROTO_MAX_PAYLOAD = 512 B). Use :meth:`drain_all` to
        loop until empty.
        """
        if max_count < 1:
            max_count = 1
        if max_count > DRAIN_MAX_COUNT:
            max_count = DRAIN_MAX_COUNT
        rpl = self._send(CMD_DRAIN, bytes([max_count]))
        if not rpl:
            return []
        n = rpl[0]
        out: list[CampaignResult] = []
        for i in range(n):
            off = 1 + i * RECORD_LEN
            chunk = rpl[off : off + RECORD_LEN]
            step_n, d, w, p = struct.unpack("<4I", chunk[0:16])
            fire_status   = chunk[16]
            verify_status = chunk[17]
            # chunk[18:20] reserved
            target_state, ts_us = struct.unpack("<II", chunk[20:28])
            out.append(
                CampaignResult(
                    step_n=step_n, delay=d, width=w, power=p,
                    fire_status=fire_status, verify_status=verify_status,
                    target_state=target_state, ts_us=ts_us,
                )
            )
        return out

    def drain_all(self) -> Iterator[CampaignResult]:
        """Yield results until the firmware ring is empty."""
        while True:
            batch = self.drain(DRAIN_MAX_COUNT)
            if not batch:
                return
            yield from batch
            if len(batch) < DRAIN_MAX_COUNT:
                return

    def watch(
        self,
        every_ms: int = 200,
        gap_ms: int = 30,
    ) -> Iterator[tuple[CampaignStatus, list[CampaignResult]]]:
        """Yield ``(status, results_drained_this_iter)`` until DONE /
        STOPPED / ERROR.

        The ``gap_ms`` between STATUS and DRAIN exists for the same
        reason ``tools/campaign_client.py`` does: when the firmware's
        executor is mid-step, dispatch ordering of two back-to-back
        replies can race. 30 ms is well below a useful poll period.
        See ``docs/MUTEX_INTERNALS.md §5``.
        """
        while True:
            st = self.status()
            time.sleep(gap_ms / 1000.0)
            results = self.drain(DRAIN_MAX_COUNT)
            yield st, results
            if st.state in (CampaignState.DONE, CampaignState.STOPPED, CampaignState.ERROR):
                return
            time.sleep(every_ms / 1000.0)

    # -- internals ----------------------------------------------------

    @staticmethod
    def _check_proto_status(reply: bytes) -> None:
        if not reply:
            raise CampaignError(0xFE, "empty proto status reply")
        code = reply[0]
        if code != ProtoStatus.OK:
            try:
                ps = ProtoStatus(code)
                raise CampaignError(code, f"campaign proto: {ps.name}")
            except ValueError:
                raise CampaignError(code) from None


__all__ = [
    "CMD_CONFIG",
    "CMD_START",
    "CMD_STOP",
    "CMD_STATUS",
    "CMD_DRAIN",
    "CONFIG_PAYLOAD_LEN",
    "STATUS_REPLY_LEN",
    "RECORD_LEN",
    "DRAIN_MAX_COUNT",
    "Engine",
    "CampaignState",
    "CampaignErr",
    "ProtoStatus",
    "CampaignStatus",
    "CampaignResult",
    "CampaignError",
    "CampaignClient",
]

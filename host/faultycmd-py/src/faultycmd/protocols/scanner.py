"""CDC2 text-shell wrapper.

Consolidates ``tools/{swd,jtag,scanner}_diag.py`` into one client.
The CDC2 shell hosts F6 SWD + F8-1 JTAG + F8-2 pinout scanner +
F8-3 unified menu + F8-4 BusPirate entry + F8-5 serprog entry +
F9-3 campaign demo, all line-buffered text. Replies are
prefix-tagged so the client can demux:

    SHELL:    top-level help / unknown command
    SWD:      F6 swd_* commands
    JTAG:     F8-1 jtag_* commands
    SCAN:     F8-2 scan jtag / scan swd
    BPIRATE:  F8-4 binary-mode entry confirmation
    SERPROG:  F8-5 binary-mode entry confirmation
    CAMPAIGN: F9-3 demo crowbar + status + drain + stop

This module wraps that line shell with one method per command.
Once a binary mode is entered (``buspirate_enter`` / ``serprog_enter``)
the operator is expected to switch the underlying port to
OpenOCD / flashrom / similar — this client does NOT try to drive
the binary protocol from inside Python; the F10-2 campaign module
handles its own binary surface and the BusPirate / serprog modes
have native external clients (OpenOCD, flashrom).
"""
from __future__ import annotations

import time
from collections.abc import Callable, Iterable
from typing import Protocol

from ..usb import cdc_for


class _SerialLike(Protocol):
    def write(self, data: bytes) -> int: ...
    def read(self, size: int = 1) -> bytes: ...
    def reset_input_buffer(self) -> None: ...
    def close(self) -> None: ...


SerialFactory = Callable[[str, int, float], _SerialLike]


def _default_serial_factory(port: str, baud: int, per_byte_timeout: float) -> _SerialLike:
    import serial  # noqa: PLC0415

    return serial.Serial(port, baud, timeout=per_byte_timeout)


# Prefixes the firmware uses for shell replies (frozen at F8-3).
ACCEPTED_PREFIXES: tuple[str, ...] = (
    "SHELL:",
    "SWD:",
    "JTAG:",
    "SCAN:",
    "BPIRATE:",
    "SERPROG:",
    "CAMPAIGN:",
)


class ScannerError(Exception):
    """Raised when a shell reply parses cleanly but the firmware
    reports ``ERR ...`` as the verb. ``.line`` holds the full reply
    so the caller can see the error tail."""

    def __init__(self, line: str) -> None:
        self.line = line
        super().__init__(line)


class ScannerClient:
    """Line-buffered CDC2 text-shell client.

    Lifecycle is the same context-manager shape as
    :class:`BinaryProtoClient`. The reply matcher is tunable per-call
    via ``accept_prefixes`` so a SWD command can ignore stray JTAG
    snapshot lines from the diag stream and vice-versa.
    """

    DEFAULT_BAUD = 115200
    DEFAULT_TIMEOUT = 3.0
    PER_BYTE_TIMEOUT = 0.2

    def __init__(
        self,
        port: str,
        *,
        baud: int = DEFAULT_BAUD,
        timeout: float = DEFAULT_TIMEOUT,
        serial_factory: SerialFactory | None = None,
    ) -> None:
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._factory = serial_factory or _default_serial_factory
        self._ser: _SerialLike | None = None

    # -- lifecycle ---------------------------------------------------

    def open(self) -> None:
        if self._ser is None:
            self._ser = self._factory(self.port, self.baud, self.PER_BYTE_TIMEOUT)

    def close(self) -> None:
        if self._ser is not None:
            self._ser.close()
            self._ser = None

    def __enter__(self) -> ScannerClient:
        self.open()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    @classmethod
    def discover(cls, **kw: object) -> ScannerClient:
        return cls(cdc_for("scanner"), **kw)   # type: ignore[arg-type]

    def _require_serial(self) -> _SerialLike:
        if self._ser is None:
            raise RuntimeError(
                "client not open — use as a context manager or call open() first"
            )
        return self._ser

    # -- low-level send / receive -----------------------------------

    def send_line(
        self,
        line: str,
        *,
        accept_prefixes: Iterable[str] = ACCEPTED_PREFIXES,
        timeout: float | None = None,
    ) -> str:
        """Send ``line\\r\\n``, return the first reply line whose
        prefix matches ``accept_prefixes``.

        The CDC2 stream interleaves periodic diag snapshots ("ADC=..."
        etc.). The prefix filter throws those away.
        """
        ser = self._require_serial()
        prefixes = tuple(accept_prefixes)
        ser.reset_input_buffer()
        ser.write((line + "\r\n").encode())
        deadline = time.time() + (timeout if timeout is not None else self.timeout)
        buf = ""
        while time.time() < deadline:
            chunk = ser.read(64)
            if not chunk:
                continue
            buf += chunk.decode(errors="replace")
            while "\n" in buf:
                line_text, _, buf = buf.partition("\n")
                stripped = line_text.strip()
                if any(stripped.startswith(p) for p in prefixes):
                    return stripped
        raise TimeoutError(f"no shell reply for {line!r}")

    def send_line_collect(
        self,
        line: str,
        *,
        accept_prefixes: Iterable[str] = ACCEPTED_PREFIXES,
        terminal_substrings: Iterable[str] = (),
        quiet_ms: int = 200,
        timeout: float | None = None,
        on_line: Callable[[str], None] | None = None,
    ) -> list[str]:
        """Send ``line``, collect every prefix-matching reply line.

        Stops when (a) any reply line contains a substring in
        ``terminal_substrings``, OR (b) ``quiet_ms`` of silence after
        at least one matching line was seen, OR (c) ``timeout``
        elapses (raises TimeoutError).
        """
        ser = self._require_serial()
        prefixes = tuple(accept_prefixes)
        terminals = tuple(terminal_substrings)
        ser.reset_input_buffer()
        ser.write((line + "\r\n").encode())
        deadline = time.time() + (timeout if timeout is not None else self.timeout)
        out: list[str] = []
        buf = ""
        last_match_at: float | None = None
        while time.time() < deadline:
            chunk = ser.read(64)
            if chunk:
                buf += chunk.decode(errors="replace")
                while "\n" in buf:
                    line_text, _, buf = buf.partition("\n")
                    stripped = line_text.strip()
                    if any(stripped.startswith(p) for p in prefixes):
                        out.append(stripped)
                        last_match_at = time.time()
                        if on_line is not None:
                            on_line(stripped)
                        if any(t in stripped for t in terminals):
                            return out
                continue
            # No new bytes — apply the quiet-timeout if we already
            # collected at least one line.
            if last_match_at is not None and (time.time() - last_match_at) * 1000 > quiet_ms:
                return out
            time.sleep(0.01)
        if out:
            return out
        raise TimeoutError(f"no shell reply for {line!r}")

    # -- SWD (F6) ----------------------------------------------------

    def swd_init(self, swclk_gp: int = 0, swdio_gp: int = 1,
                 nrst_gp: int | None = 2) -> str:
        if nrst_gp is None:
            cmd = f"swd init {swclk_gp} {swdio_gp}"
        else:
            cmd = f"swd init {swclk_gp} {swdio_gp} {nrst_gp}"
        return self._expect_ok("SWD:", cmd)

    def swd_deinit(self) -> str:
        return self._expect_ok("SWD:", "swd deinit")

    def swd_freq(self, khz: int) -> str:
        return self._expect_ok("SWD:", f"swd freq {khz}")

    def swd_connect(self) -> tuple[str, int | None]:
        """Returns (raw_line, dpidr_or_None)."""
        line = self.send_line("swd connect", accept_prefixes=("SWD:",))
        return line, _parse_hex_after(line, "dpidr=")

    def swd_read32(self, addr: int) -> tuple[str, int | None]:
        line = self.send_line(f"swd read32 0x{addr:08X}", accept_prefixes=("SWD:",))
        return line, _parse_hex_after(line, "]=")

    def swd_write32(self, addr: int, value: int) -> str:
        return self._expect_ok(
            "SWD:", f"swd write32 0x{addr:08X} 0x{value:08X}"
        )

    def swd_reset(self, asserted: bool) -> str:
        return self._expect_ok("SWD:", f"swd reset {1 if asserted else 0}")

    # -- JTAG (F8-1) -------------------------------------------------

    def jtag_init(
        self, tdi: int, tdo: int, tms: int, tck: int,
        trst: int | None = None,
    ) -> str:
        parts = ["jtag", "init", str(tdi), str(tdo), str(tms), str(tck)]
        if trst is not None:
            parts.append(str(trst))
        return self._expect_ok("JTAG:", " ".join(parts))

    def jtag_deinit(self) -> str:
        return self._expect_ok("JTAG:", "jtag deinit")

    def jtag_reset(self) -> str:
        return self._expect_ok("JTAG:", "jtag reset")

    def jtag_trst(self) -> str:
        return self._expect_ok("JTAG:", "jtag trst")

    def jtag_chain(self) -> tuple[str, int | None]:
        line = self.send_line("jtag chain", accept_prefixes=("JTAG:",))
        return line, _parse_int_after(line, "devices=")

    def jtag_idcode(self) -> list[str]:
        """Returns every JTAG: line emitted by the multi-line response.

        First line is ``JTAG: OK idcodes count=N``; the next N lines
        each describe one IDCODE.
        """
        return self.send_line_collect(
            "jtag idcode",
            accept_prefixes=("JTAG:",),
            quiet_ms=300,
            timeout=5.0,
        )

    # -- SCAN (F8-2) -------------------------------------------------

    def scan_jtag(
        self,
        timeout_s: float = 30.0,
        on_progress: Callable[[str], None] | None = None,
    ) -> list[str]:
        return self.send_line_collect(
            "scan jtag",
            accept_prefixes=("SCAN:",),
            terminal_substrings=("MATCH", "NO_MATCH", "ERR"),
            timeout=timeout_s,
            on_line=on_progress,
        )

    def scan_swd(
        self,
        targetsel_hex: str | None = None,
        timeout_s: float = 30.0,
        on_progress: Callable[[str], None] | None = None,
    ) -> list[str]:
        cmd = "scan swd" if targetsel_hex is None else f"scan swd {targetsel_hex}"
        return self.send_line_collect(
            cmd,
            accept_prefixes=("SCAN:",),
            terminal_substrings=("MATCH", "NO_MATCH", "ERR"),
            timeout=timeout_s,
            on_line=on_progress,
        )

    # -- Mode switches (F8-4 / F8-5) — confirmation-only -----------

    def buspirate_enter(
        self,
        tdi: int = 0, tdo: int = 1, tms: int = 2, tck: int = 3,
    ) -> str:
        """Send `buspirate enter`. After this the operator points
        OpenOCD at the same port; the Python client should NOT keep
        sending text-shell commands until the BusPirate session
        ends with 0x0F."""
        return self.send_line(
            f"buspirate enter {tdi} {tdo} {tms} {tck}",
            accept_prefixes=("BPIRATE:",),
        )

    def serprog_enter(
        self,
        cs: int = 0, mosi: int = 1, miso: int = 2, sck: int = 3,
    ) -> str:
        """Send `serprog enter`. After this point flashrom at the
        same port. The firmware exits the binary mode automatically
        on DTR drop (host disconnect)."""
        return self.send_line(
            f"serprog enter {cs} {mosi} {miso} {sck}",
            accept_prefixes=("SERPROG:",),
        )

    # -- internals --------------------------------------------------

    def _expect_ok(self, prefix: str, cmd: str) -> str:
        line = self.send_line(cmd, accept_prefixes=(prefix,))
        if " ERR " in f" {line} ":
            raise ScannerError(line)
        return line


def _parse_hex_after(line: str, marker: str) -> int | None:
    """Find ``marker`` in ``line`` and parse the hex token that follows."""
    idx = line.find(marker)
    if idx < 0:
        return None
    rest = line[idx + len(marker):].strip()
    token = rest.split()[0] if rest else ""
    if not token:
        return None
    try:
        return int(token, 16)
    except ValueError:
        return None


def _parse_int_after(line: str, marker: str) -> int | None:
    """Find ``marker`` and parse the decimal token that follows."""
    idx = line.find(marker)
    if idx < 0:
        return None
    rest = line[idx + len(marker):].strip()
    token = rest.split()[0] if rest else ""
    if not token:
        return None
    try:
        return int(token, 10)
    except ValueError:
        return None


__all__ = [
    "ACCEPTED_PREFIXES",
    "ScannerClient",
    "ScannerError",
]

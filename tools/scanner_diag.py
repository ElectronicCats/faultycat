#!/usr/bin/env python3
"""FaultyCat v3 pinout scanner client (F8-2).

Drives the `scan jtag` and `scan swd` commands of the CDC2 shell. The
scan iterates hundreds of pinout permutations and prints progress
along the way; this client streams every SCAN: line until the final
MATCH or NO_MATCH verdict.

Usage:
  tools/scanner_diag.py --port /dev/ttyACM4 jtag
  tools/scanner_diag.py --port /dev/ttyACM4 swd
  tools/scanner_diag.py --port /dev/ttyACM4 swd --targetsel 0x01002927

Note: SWD scan against a v2.x scanner header is currently expected to
fail end-to-end on real hardware due to the TXS0108E level-shifter
bidirectional bug documented in HARDWARE_V2.md §2 — same gate that
blocks the F6 SWD physical validation. JTAG scan is unaffected
(unidirectional push-pull).
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")


_ACCEPTED_PREFIXES = ("SCAN:", "SHELL:")
_TERMINAL_LINES = ("MATCH", "NO_MATCH", "ERR")


def stream_scan(ser: "serial.Serial", cmd: str, timeout: float) -> None:
    """Send `cmd` and stream every SCAN: line until a terminal verdict."""
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    deadline = time.time() + timeout
    line = ""
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        c = b.decode(errors="replace")
        if c == "\n":
            stripped = line.strip()
            if any(stripped.startswith(p) for p in _ACCEPTED_PREFIXES):
                print(stripped)
                if any(tag in stripped for tag in _TERMINAL_LINES):
                    return
            line = ""
        elif c == "\r":
            pass
        else:
            line += c
    raise TimeoutError(f"scan {cmd!r} did not produce a terminal line "
                       f"within {timeout:.1f} s")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyACM4",
                   help="CDC2 device node (Scanner Shell)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--timeout", type=float, default=60.0,
                   help="upper bound on scan time (default 60 s)")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("jtag")
    swd = sub.add_parser("swd")
    swd.add_argument("--targetsel", default=None,
                     help="hex SWD multidrop TARGETSEL (defaults to "
                          "RP2040 CORE0 0x01002927)")

    args = p.parse_args()
    if args.cmd == "jtag":
        cmd = "scan jtag"
    elif args.cmd == "swd":
        cmd = "scan swd"
        if args.targetsel is not None:
            ts = int(args.targetsel, 16)
            cmd += f" {ts:08X}"
    else:
        sys.exit(f"unknown sub-command: {args.cmd}")

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        try:
            stream_scan(ser, cmd, args.timeout)
        except TimeoutError as e:
            sys.exit(f"timeout: {e}")


if __name__ == "__main__":
    main()

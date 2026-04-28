#!/usr/bin/env python3
"""FaultyCat v3 JTAG diagnostic client (F8-1).

Talks the line-buffered text shell on CDC2 ("Scanner Shell") that
F6-5 added and F8-1 extended with `jtag <subcmd>`. Reference tool
only — F8-4 will expose BusPirate binary mode for OpenOCD-driven JTAG
sessions.

Usage:
  tools/jtag_diag.py --port /dev/ttyACM4 init 0 1 2 3
  tools/jtag_diag.py --port /dev/ttyACM4 init 0 1 2 3 4    # with TRST
  tools/jtag_diag.py --port /dev/ttyACM4 reset
  tools/jtag_diag.py --port /dev/ttyACM4 chain
  tools/jtag_diag.py --port /dev/ttyACM4 idcode
  tools/jtag_diag.py --port /dev/ttyACM4 trst
  tools/jtag_diag.py --port /dev/ttyACM4 deinit
  tools/jtag_diag.py --port /dev/ttyACM4 help

The CDC2 stream interleaves the periodic snapshot diag line and the
banner; this client filters those out and returns only lines that
start with "JTAG:" or "SHELL:" (the prefix conventions the firmware
uses).

Note: SWD and JTAG share the scanner header (GP0..GP7) — only one
may be active at a time. The firmware soft-locks: `jtag init` while
SWD is held returns `JTAG: ERR swd_in_use`. F9 promotes this to a
real mutex_t.
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")


_ACCEPTED_PREFIXES = ("JTAG:", "SHELL:")


def send_cmd(ser: "serial.Serial", cmd: str, timeout: float = 3.0) -> list[str]:
    """Send `cmd`, collect every JTAG:/SHELL: line until quiet for 200 ms."""
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    deadline = time.time() + timeout
    quiet_until = None
    lines: list[str] = []
    line = ""
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            if lines and quiet_until and time.time() > quiet_until:
                return lines
            continue
        quiet_until = time.time() + 0.2
        c = b.decode(errors="replace")
        if c == "\n":
            stripped = line.strip()
            if any(stripped.startswith(p) for p in _ACCEPTED_PREFIXES):
                lines.append(stripped)
                # idcode replies span N+1 lines; keep collecting until
                # the 200 ms quiet window elapses.
            line = ""
        elif c == "\r":
            pass
        else:
            line += c
    if lines:
        return lines
    raise TimeoutError(f"no JTAG response to {cmd!r}")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyACM4",
                   help="CDC2 device node (Scanner Shell)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--timeout", type=float, default=3.0)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("help")
    sub.add_parser("deinit")
    sub.add_parser("reset",  help="TAP reset → Run-Test/Idle")
    sub.add_parser("trst",   help="pulse TRST low ~1 ms (no-op if no TRST)")
    sub.add_parser("chain",  help="detect # of TAPs in chain")
    sub.add_parser("idcode", help="read the IDCODE chain")

    init = sub.add_parser("init")
    init.add_argument("tdi", type=int)
    init.add_argument("tdo", type=int)
    init.add_argument("tms", type=int)
    init.add_argument("tck", type=int)
    init.add_argument("trst", type=int, nargs="?", default=None)

    args = p.parse_args()

    if args.cmd == "help":
        cmd = "?"
    elif args.cmd == "init":
        parts = ["jtag", "init", str(args.tdi), str(args.tdo),
                 str(args.tms), str(args.tck)]
        if args.trst is not None:
            parts.append(str(args.trst))
        cmd = " ".join(parts)
    elif args.cmd in ("deinit", "reset", "trst", "chain", "idcode"):
        cmd = f"jtag {args.cmd}"
    else:
        sys.exit(f"unknown sub-command: {args.cmd}")

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        try:
            for line in send_cmd(ser, cmd, args.timeout):
                print(line)
        except TimeoutError as e:
            sys.exit(f"timeout: {e}")


if __name__ == "__main__":
    main()

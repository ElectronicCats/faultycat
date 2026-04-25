#!/usr/bin/env python3
"""FaultyCat v3 SWD diagnostic client (F6-5).

Talks the line-buffered text shell on CDC2 ("Scanner Shell") that
F6-5 added. Reference tool only — F7 will expose CMSIS-DAP on the
Vendor IF for OpenOCD / probe-rs / pyOCD.

Usage:
  tools/swd_diag.py --port /dev/ttyACM4 connect
  tools/swd_diag.py --port /dev/ttyACM4 read32 0xE000ED00
  tools/swd_diag.py --port /dev/ttyACM4 write32 0x20000000 0xCAFEBABE
  tools/swd_diag.py --port /dev/ttyACM4 freq 4000
  tools/swd_diag.py --port /dev/ttyACM4 reset 1
  tools/swd_diag.py --port /dev/ttyACM4 reset 0
  tools/swd_diag.py --port /dev/ttyACM4 init 0 1 2
  tools/swd_diag.py --port /dev/ttyACM4 deinit
  tools/swd_diag.py --port /dev/ttyACM4 help

The CDC2 stream interleaves the periodic snapshot diag line and the
banner; this client filters those out and returns only lines that
start with "SWD:" (the convention the firmware uses for shell
output).
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")


def send_cmd(ser: "serial.Serial", cmd: str, timeout: float = 3.0) -> str:
    """Send `cmd` and return the first line beginning with 'SWD:'."""
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
            if stripped.startswith("SWD:"):
                return stripped
            line = ""
        elif c == "\r":
            pass
        else:
            line += c
    raise TimeoutError(f"no SWD response to {cmd!r}")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--port", default="/dev/ttyACM4",
                   help="CDC2 device node (Scanner Shell)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--timeout", type=float, default=3.0)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("help")
    sub.add_parser("connect")
    sub.add_parser("probe", help="alias of connect")
    sub.add_parser("deinit")

    init = sub.add_parser("init")
    init.add_argument("swclk", type=int)
    init.add_argument("swdio", type=int)
    init.add_argument("nrst", type=int, nargs="?", default=None)

    freq = sub.add_parser("freq")
    freq.add_argument("khz", type=int)

    r32 = sub.add_parser("read32")
    r32.add_argument("addr", help="32-bit hex (e.g. 0xE000ED00)")

    w32 = sub.add_parser("write32")
    w32.add_argument("addr", help="32-bit hex")
    w32.add_argument("value", help="32-bit hex")

    rst = sub.add_parser("reset")
    rst.add_argument("state", choices=["0", "1"],
                     help="1 = assert nRST low, 0 = release")

    args = p.parse_args()

    if args.cmd == "help":
        cmd = "?"
    elif args.cmd == "init":
        if args.nrst is None:
            cmd = f"swd init {args.swclk} {args.swdio}"
        else:
            cmd = f"swd init {args.swclk} {args.swdio} {args.nrst}"
    elif args.cmd == "deinit":
        cmd = "swd deinit"
    elif args.cmd == "freq":
        cmd = f"swd freq {args.khz}"
    elif args.cmd in ("connect", "probe"):
        cmd = f"swd {args.cmd}"
    elif args.cmd == "read32":
        cmd = f"swd read32 {args.addr}"
    elif args.cmd == "write32":
        cmd = f"swd write32 {args.addr} {args.value}"
    elif args.cmd == "reset":
        cmd = f"swd reset {args.state}"
    else:
        sys.exit(f"unknown sub-command: {args.cmd}")

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        try:
            print(send_cmd(ser, cmd, args.timeout))
        except TimeoutError as e:
            sys.exit(f"timeout: {e}")


if __name__ == "__main__":
    main()

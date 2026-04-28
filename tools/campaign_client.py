#!/usr/bin/env python3
"""FaultyCat v3 minimal campaign host client (F9-5).

Drives `services/campaign_manager` over the F9-4 binary protocol —
multiplexed on CDC0 (EMFI campaigns) or CDC1 (crowbar campaigns).
The CDC determines the engine; the wire format is identical.

Subcommands:

  ping <port>                       round-trip the underlying proto
                                    (uses emfi_proto / crowbar_proto's
                                    PING — handy to confirm the right
                                    CDC is selected before campaigning).

  configure <port> --engine X
            --delay START:END:STEP
            --width START:END:STEP
            --power START:END:STEP
            [--settle MS]           configure a sweep. `step=0` collapses
                                    the axis to its start value.

  start  <port>                     transition CONFIGURING → SWEEPING.

  stop   <port>                     halt mid-sweep.

  status <port>                     fetch state + counters.

  drain  <port> [--max N]           pop up to N results (cap 18 per
                                    proto frame). Loops automatically
                                    until the ring is empty.

  watch  <port> [--max N] [--every MS]
                                    poll status + drain in a loop.
                                    Exits on DONE / STOPPED / ERROR.

NOT the F10 Rust client. Reference / smoke tool only.

Engine knobs:
  EMFI:    delay µs, width µs (1..50). power axis ignored.
  Crowbar: delay µs, width ns (8..50000). power 1=LP, 2=HP.
"""
from __future__ import annotations

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pip install pyserial")


SOF = 0xFA

# F9-4 opcode numbers. Same on emfi_proto and crowbar_proto.
CMD = {
    "ping":     0x01,
    "config":   0x20,
    "start":    0x21,
    "stop":     0x22,
    "status":   0x23,
    "drain":    0x24,
}

ENGINE_EMFI    = 0
ENGINE_CROWBAR = 1

STATE = ["IDLE", "CONFIGURING", "SWEEPING", "DONE", "STOPPED", "ERROR"]
ERR   = ["NONE", "BAD_CONFIG", "NOT_CONFIGURED",
         "BUS_BUSY", "STEP_FAILED", "INTERNAL"]

PROTO_STATUS_LABEL = {
    0x00: "OK",
    0x01: "ERR_BAD_LEN",
    0x02: "ERR_REJECTED",
}

DEFAULT_PORT_EMFI    = "/dev/ttyACM0"
DEFAULT_PORT_CROWBAR = "/dev/ttyACM1"


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([cmd]) + struct.pack("<H", len(payload)) + payload
    crc = crc16_ccitt(body)
    return bytes([SOF]) + body + struct.pack("<H", crc)


def read_frame(ser, timeout: float = 2.0) -> tuple[int, bytes]:
    deadline = time.time() + timeout
    while time.time() < deadline:
        b = ser.read(1)
        if b and b[0] == SOF:
            header = ser.read(3)
            if len(header) != 3:
                continue
            cmd = header[0]
            length = struct.unpack("<H", header[1:3])[0]
            payload = ser.read(length)
            crc_bytes = ser.read(2)
            if len(payload) != length or len(crc_bytes) != 2:
                continue
            expected = struct.unpack("<H", crc_bytes)[0]
            calc = crc16_ccitt(header + payload)
            if expected != calc:
                continue
            return cmd, payload
    raise TimeoutError("no reply")


def parse_axis(s: str) -> tuple[int, int, int]:
    """Parse 'START:END:STEP' (each int). 'X' alone collapses to (X, X, 0)."""
    if ":" not in s:
        v = int(s, 0)
        return v, v, 0
    parts = s.split(":")
    if len(parts) != 3:
        raise ValueError(f"bad axis: {s!r} (want START:END:STEP)")
    return int(parts[0], 0), int(parts[1], 0), int(parts[2], 0)


def encode_config(delay: tuple[int, int, int],
                  width: tuple[int, int, int],
                  power: tuple[int, int, int],
                  settle_ms: int) -> bytes:
    return struct.pack("<10I",
                       delay[0], delay[1], delay[2],
                       width[0], width[1], width[2],
                       power[0], power[1], power[2],
                       settle_ms)


def decode_status(payload: bytes) -> dict:
    if len(payload) != 20:
        raise ValueError(f"bad status payload length {len(payload)}")
    state    = payload[0]
    err      = payload[1]
    step_n, total, pushed, dropped = struct.unpack("<4I", payload[4:20])
    return {
        "state": STATE[state] if state < len(STATE) else state,
        "err":   ERR[err]   if err   < len(ERR)   else err,
        "step":  step_n, "total": total,
        "pushed": pushed, "dropped": dropped,
    }


def decode_results(payload: bytes) -> list[dict]:
    if len(payload) < 1:
        return []
    n = payload[0]
    out = []
    rec_size = 28
    for i in range(n):
        off = 1 + i * rec_size
        step_n, d, w, p = struct.unpack("<4I", payload[off:off+16])
        fire   = payload[off+16]
        verify = payload[off+17]
        target, ts = struct.unpack("<II", payload[off+20:off+28])
        out.append({
            "step": step_n, "delay": d, "width": w, "power": p,
            "fire": fire, "verify": verify,
            "target": target, "ts_us": ts,
        })
    return out


# ---------------------------------------------------------------------------
# Subcommand implementations
# ---------------------------------------------------------------------------

def do_ping(args):
    with serial.Serial(args.port, 115200, timeout=1.0) as ser:
        ser.write(frame(CMD["ping"]))
        _, pl = read_frame(ser)
        print(f"PONG {pl!r}")


def do_configure(args):
    payload = encode_config(parse_axis(args.delay),
                            parse_axis(args.width),
                            parse_axis(args.power),
                            args.settle)
    with serial.Serial(args.port, 115200, timeout=1.0) as ser:
        ser.write(frame(CMD["config"], payload))
        _, r = read_frame(ser)
        st = r[0] if r else 0xFF
        print(f"configure -> {PROTO_STATUS_LABEL.get(st, hex(st))}")


def do_simple(args, op: str):
    with serial.Serial(args.port, 115200, timeout=1.0) as ser:
        ser.write(frame(CMD[op]))
        _, r = read_frame(ser)
        st = r[0] if r else 0xFF
        print(f"{op} -> {PROTO_STATUS_LABEL.get(st, hex(st))}")


def do_status(args):
    with serial.Serial(args.port, 115200, timeout=1.0) as ser:
        ser.write(frame(CMD["status"]))
        _, r = read_frame(ser)
        s = decode_status(r)
        print(f"state={s['state']} err={s['err']} "
              f"step={s['step']}/{s['total']} "
              f"pushed={s['pushed']} dropped={s['dropped']}")


def do_drain(args):
    with serial.Serial(args.port, 115200, timeout=2.0) as ser:
        total = 0
        while True:
            ser.write(frame(CMD["drain"], bytes([min(args.max, 18)])))
            _, r = read_frame(ser)
            results = decode_results(r)
            for res in results:
                print(f"step={res['step']} d={res['delay']} w={res['width']} "
                      f"p={res['power']} fire=0x{res['fire']:02X} "
                      f"verify=0x{res['verify']:02X} "
                      f"target=0x{res['target']:08X} ts={res['ts_us']}us")
                total += 1
            if len(results) < min(args.max, 18):
                break
        print(f"drained {total} result(s)")


def do_watch(args):
    """Poll status + drain in a loop until DONE / STOPPED / ERROR.

    Note: the ~30 ms gap between the STATUS read and the DRAIN write
    is deliberate. Without it, when the firmware is mid-step (executor
    blocked in the engine's fire loop), the DRAIN reply occasionally
    times out — the second request lands in the CDC RX while pump_
    crowbar_cdc is still completing the STATUS dispatch on this same
    yield slice, and the order in which TinyUSB schedules the two
    replies vs the host's read window can race. 30 ms is well below
    a useful poll period and removes the race in practice. Long-term
    fix is splitting the engine wait loop so it doesn't park dispatch
    against itself, but that's a F-future cleanup."""
    with serial.Serial(args.port, 115200, timeout=2.0) as ser:
        while True:
            ser.write(frame(CMD["status"]))
            _, r = read_frame(ser)
            s = decode_status(r)

            time.sleep(0.03)
            ser.write(frame(CMD["drain"], bytes([min(args.max, 18)])))
            _, dr = read_frame(ser)
            for res in decode_results(dr):
                print(f"  step={res['step']} d={res['delay']} w={res['width']} "
                      f"p={res['power']} fire=0x{res['fire']:02X} "
                      f"verify=0x{res['verify']:02X}")

            print(f"[watch] {s['state']} {s['step']}/{s['total']} "
                  f"pushed={s['pushed']} dropped={s['dropped']}")

            if s["state"] in ("DONE", "STOPPED", "ERROR"):
                break
            time.sleep(args.every / 1000.0)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    p = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    def add_port(parser):
        parser.add_argument("--port", default=DEFAULT_PORT_CROWBAR,
                            help=f"CDC0 (EMFI) {DEFAULT_PORT_EMFI} or "
                                 f"CDC1 (crowbar) {DEFAULT_PORT_CROWBAR}")

    add_port(sub.add_parser("ping"))

    c = sub.add_parser("configure")
    add_port(c)
    c.add_argument("--engine", choices=["emfi", "crowbar"],
                   help="(informational only — engine is implied by the CDC)")
    c.add_argument("--delay", required=True,
                   help="START:END:STEP in µs (or single int)")
    c.add_argument("--width", required=True,
                   help="START:END:STEP — EMFI µs / crowbar ns")
    c.add_argument("--power", required=True,
                   help="START:END:STEP — crowbar 1=LP / 2=HP; EMFI ignored")
    c.add_argument("--settle", type=int, default=0,
                   help="ms between fires (default 0)")

    add_port(sub.add_parser("start"))
    add_port(sub.add_parser("stop"))
    add_port(sub.add_parser("status"))

    d = sub.add_parser("drain")
    add_port(d)
    d.add_argument("--max", type=int, default=18,
                   help="results per request (cap 18)")

    w = sub.add_parser("watch")
    add_port(w)
    w.add_argument("--max", type=int, default=18)
    w.add_argument("--every", type=int, default=200,
                   help="poll interval in ms (default 200)")

    args = p.parse_args()

    if args.cmd == "ping":
        do_ping(args)
    elif args.cmd == "configure":
        do_configure(args)
    elif args.cmd in ("start", "stop"):
        do_simple(args, args.cmd)
    elif args.cmd == "status":
        do_status(args)
    elif args.cmd == "drain":
        do_drain(args)
    elif args.cmd == "watch":
        do_watch(args)


if __name__ == "__main__":
    main()

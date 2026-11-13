#!/usr/bin/env python3
"""Decode binary telemetry from rtos-condition-monitor.

Frames are CRC-16/CCITT-FALSE protected, COBS-encoded and 0x00-delimited
(see lib/include/telemetry.h). Text lines (CLI output, CSV telemetry) that
share the same UART are passed through unchanged, so you can leave
`tlm bin` running and still use the shell.

Usage:
    python tools/tlm_decode.py COM5            # live, needs pyserial
    python tools/tlm_decode.py /dev/ttyACM0 --csv out.csv
    python tools/tlm_decode.py --file capture.bin
"""
from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from typing import Iterator, Optional

TLM_VERSION = 1
TLM_TYPE_SNAPSHOT = 1
RAW_LEN = 24
_FMT = "<BBHIhHHHHHBBH"  # must match tlm_pack() in lib/telemetry.c


def crc16_ccitt(data: bytes, crc: int = 0xFFFF) -> int:
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    out = bytearray([0])
    code_idx, code = 0, 1
    for i, b in enumerate(data):
        if b:
            out.append(b)
            code += 1
        if b == 0 or code == 0xFF:
            out[code_idx] = code
            code, code_idx = 1, len(out)
            if b == 0 or i + 1 < len(data):
                out.append(0)
    if code_idx < len(out):
        out[code_idx] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        i += 1
        if code == 0 or i + code - 1 > len(data):
            raise ValueError("malformed COBS block")
        block = data[i : i + code - 1]
        if 0 in block:
            raise ValueError("zero inside COBS block")
        out += block
        i += code - 1
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


@dataclass
class Snapshot:
    seq: int
    uptime_ms: int
    temp_c: float
    vdda_mv: int
    ain_mv: int
    ain_min_mv: int
    ain_max_mv: int
    ain_rms_mv: int
    alarms: int
    cpu_pct: int

    @property
    def alarm_text(self) -> str:
        names = ("TEMP", "VDDA", "RMS")
        active = [n for i, n in enumerate(names) if self.alarms & (1 << i)]
        latched = [n for i, n in enumerate(names) if self.alarms & (1 << (i + 4))]
        return f"active={','.join(active) or '-'} latched={','.join(latched) or '-'}"


def pack(s: Snapshot) -> bytes:
    body = struct.pack(
        _FMT[:-1], TLM_VERSION, TLM_TYPE_SNAPSHOT, s.seq, s.uptime_ms, round(s.temp_c * 100),
        s.vdda_mv, s.ain_mv, s.ain_min_mv, s.ain_max_mv, s.ain_rms_mv, s.alarms, s.cpu_pct,
    )
    return body + struct.pack("<H", crc16_ccitt(body))


def unpack(raw: bytes) -> Snapshot:
    if len(raw) != RAW_LEN:
        raise ValueError(f"bad length {len(raw)}")
    f = struct.unpack(_FMT, raw)
    if f[0] != TLM_VERSION or f[1] != TLM_TYPE_SNAPSHOT:
        raise ValueError("unknown version/type")
    if crc16_ccitt(raw[:-2]) != f[-1]:
        raise ValueError("CRC mismatch")
    return Snapshot(f[2], f[3], f[4] / 100.0, *f[5:12])


def frames(stream: Iterator[bytes]) -> Iterator[tuple[str, object]]:
    """Split a byte stream on 0x00 and yield ('frame', Snapshot) | ('text', str) | ('error', str)."""
    buf = bytearray()
    for chunk in stream:
        for b in chunk:
            if b != 0:
                buf.append(b)
                continue
            data, buf = bytes(buf), bytearray()
            # Any text printed before the frame (CLI output) ends with a newline.
            nl = data.rfind(b"\n")
            if nl >= 0:
                yield "text", data[: nl + 1].decode("ascii", "replace")
                data = data[nl + 1 :]
            if not data:
                continue
            try:
                yield "frame", unpack(cobs_decode(data))
            except ValueError as e:
                yield "error", str(e)


def main(argv: Optional[list[str]] = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port", nargs="?", help="serial port (e.g. COM5, /dev/ttyACM0)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--file", help="decode a captured binary file instead of a port")
    ap.add_argument("--csv", help="also append decoded frames to this CSV file")
    a = ap.parse_args(argv)

    if a.file:
        def stream():
            with open(a.file, "rb") as fh:
                while chunk := fh.read(4096):
                    yield chunk
    elif a.port:
        try:
            import serial  # type: ignore
        except ImportError:
            print("pyserial is required for live decoding: pip install pyserial", file=sys.stderr)
            return 2
        ser = serial.Serial(a.port, a.baud, timeout=0.2)

        def stream():
            while True:
                yield ser.read(256)
    else:
        ap.print_help()
        return 2

    out = open(a.csv, "a", encoding="ascii") if a.csv else None
    last_seq: Optional[int] = None
    dropped = 0
    try:
        for kind, item in frames(stream()):
            if kind == "text":
                sys.stdout.write(str(item))
            elif kind == "error":
                print(f"[bad frame: {item}]")
            else:
                s: Snapshot = item  # type: ignore[assignment]
                if last_seq is not None and s.seq != (last_seq + 1) & 0xFFFF:
                    dropped += (s.seq - last_seq - 1) & 0xFFFF
                last_seq = s.seq
                print(f"#{s.seq:5d} t={s.uptime_ms/1000:9.3f}s  T={s.temp_c:6.2f}C  VDDA={s.vdda_mv}mV  "
                      f"AIN={s.ain_mv}mV [{s.ain_min_mv}..{s.ain_max_mv}] rms={s.ain_rms_mv}mV  "
                      f"cpu={s.cpu_pct}%  {s.alarm_text}  dropped={dropped}")
                if out:
                    out.write(f"{s.seq},{s.uptime_ms},{s.temp_c:.2f},{s.vdda_mv},{s.ain_mv},{s.ain_min_mv},"
                              f"{s.ain_max_mv},{s.ain_rms_mv},{s.alarms},{s.cpu_pct}\n")
    except KeyboardInterrupt:
        pass
    finally:
        if out:
            out.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())

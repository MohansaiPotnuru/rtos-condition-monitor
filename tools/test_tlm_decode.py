#!/usr/bin/env python3
"""Contract tests: the Python decoder must agree byte-for-byte with lib/telemetry.c.

GOLDEN_WIRE is the same vector as k_golden_wire in tests/test_system.c.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tlm_decode as t  # noqa: E402

GOLDEN = t.Snapshot(seq=0x1234, uptime_ms=0x00BC614E, temp_c=-12.34, vdda_mv=3298, ain_mv=1650,
                    ain_min_mv=1200, ain_max_mv=2100, ain_rms_mv=318, alarms=0x13, cpu_pct=7)
GOLDEN_WIRE = bytes.fromhex("08 01 01 34 12 4E 61 BC 11 2E FB E2 0C 72 06 B0 04 34 08 3E 01 13 07 87 66 00")


class TelemetryContract(unittest.TestCase):
    def test_crc_check_value(self):
        self.assertEqual(t.crc16_ccitt(b"123456789"), 0x29B1)

    def test_encode_matches_firmware(self):
        self.assertEqual(t.cobs_encode(t.pack(GOLDEN)) + b"\x00", GOLDEN_WIRE)

    def test_decode_golden(self):
        s = t.unpack(t.cobs_decode(GOLDEN_WIRE[:-1]))
        self.assertEqual(s, GOLDEN)
        self.assertEqual(s.alarm_text, "active=TEMP,VDDA latched=TEMP")

    def test_cobs_roundtrip_edges(self):
        for data in (b"", b"\x00", b"\x00\x00", bytes(range(1, 255)), bytes(range(1, 256)),
                     b"\x00" + bytes(range(1, 255)), bytes(600)):
            self.assertEqual(t.cobs_decode(t.cobs_encode(data)), data)

    def test_stream_mixes_text_and_frames(self):
        stream = [b"status ok\r\n", GOLDEN_WIRE[:10], GOLDEN_WIRE[10:], b"garbage\x00"]
        out = list(t.frames(iter(stream)))
        self.assertEqual(out[0], ("text", "status ok\r\n"))
        self.assertEqual(out[1], ("frame", GOLDEN))
        self.assertEqual(out[2][0], "error")

    def test_corruption_detected(self):
        raw = bytearray(t.pack(GOLDEN))
        raw[10] ^= 0x40
        with self.assertRaises(ValueError):
            t.unpack(bytes(raw))


if __name__ == "__main__":
    unittest.main()

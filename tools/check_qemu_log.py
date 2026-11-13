#!/usr/bin/env python3
"""Assert on the UART log of a QEMU smoke run (see .github/workflows/ci.yml).

The firmware built with TARGET=qemu runs a short CLI script at boot
(version, selftest, ps, stats, alarm, tlm csv 200). This checks that the
scheduler started, every task exists, the schedulability analysis and the
self tests passed, and periodic telemetry is flowing with consecutive
sequence numbers.
"""
import re
import sys

REQUIRED = [
    (r"rtos-condition-monitor \d+\.\d+\.\d+ \| qemu-netduinoplus2 \| FreeRTOS V11", "boot banner"),
    (r"RTA \(budgeted WCET\): PASS", "response-time analysis passed"),
    (r"selftest: PASS", "self tests passed"),
    (r"^sensor\s+\w\s+4\s", "sensor task at priority 4"),
    (r"^process\s+\w\s+3\s", "process task at priority 3"),
    (r"^telemetry\s+\w\s+2\s", "telemetry task at priority 2"),
    (r"^cli\s+\w\s+1\s", "cli task at priority 1"),
    (r"^supervisor\s+\w\s+5\s", "supervisor task at priority 5"),
    (r"^IDLE\s", "idle task"),
    (r"^Tmr Svc\s", "timer daemon"),
    (r"supervisor: \d+ windows, last missing 0x0, feeding IWDG", "all tasks checking in"),
    (r"telemetry csv, every 200 ms", "telemetry switched to 200 ms"),
]
FORBIDDEN = [r"\*\*\* FATAL", r"selftest: FAIL", r"RTA .*FAIL", r"stalled"]
MIN_TLM_LINES = 5


def main(path: str) -> int:
    text = open(path, encoding="ascii", errors="replace").read().replace("\r", "")
    ok = True
    for pattern, what in REQUIRED:
        hit = re.search(pattern, text, re.MULTILINE) is not None
        print(f"{'ok  ' if hit else 'FAIL'} {what}")
        ok &= hit
    for pattern in FORBIDDEN:
        if re.search(pattern, text, re.MULTILINE):
            print(f"FAIL forbidden output: {pattern}")
            ok = False

    seqs = [int(m.group(1)) for m in re.finditer(r"^tlm,(\d+),\d+,", text, re.MULTILINE)]
    gaps = sum(1 for a, b in zip(seqs, seqs[1:]) if b != (a + 1) & 0xFFFF)
    print(f"{'ok  ' if len(seqs) >= MIN_TLM_LINES else 'FAIL'} telemetry lines: {len(seqs)} (need {MIN_TLM_LINES})")
    print(f"{'ok  ' if gaps == 0 else 'FAIL'} telemetry sequence gaps: {gaps}")
    ok &= len(seqs) >= MIN_TLM_LINES and gaps == 0

    print("\nQEMU smoke test:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "uart.log"))

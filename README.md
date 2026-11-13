# rtos-condition-monitor

[![CI](https://github.com/MohansaiPotnuru/rtos-condition-monitor/actions/workflows/ci.yml/badge.svg)](https://github.com/MohansaiPotnuru/rtos-condition-monitor/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![FreeRTOS](https://img.shields.io/badge/FreeRTOS-V11.1.0-5CB85C)
![MCU](https://img.shields.io/badge/STM32F401RE-Cortex--M4F-03234B)

**A FreeRTOS condition-monitoring node for the STM32 NUCLEO-F401RE**, written the
way production firmware should be: an analysed task set, static memory only,
supervised tasks, fault records that survive a reset, and CI that boots the
firmware in an emulator on every push.

The node samples an analog sensor at 100 Hz (vibration, current or any 0–3.3 V
signal on A0), along with the die temperature and the real supply voltage. It
computes a 1-second AC RMS, raises alarms with hysteresis, and streams
telemetry as CSV or as CRC-protected COBS frames over the ST-LINK virtual COM
port.

Boot output (the response times are what the firmware computes for its own task set):

```
rtos-condition-monitor 1.0.0 | NUCLEO-F401RE | FreeRTOS V11.1.0 | 84 MHz
boot #1, reset cause: power-on
task        prio     T(us)    C(us)    B(us)      R(us)
sensor         4     10000      150        0        470
process        3     10000      150       50        670
telemetry      2    100000     1000    10000      11920
cli            1     50000     5000        0       6620
supervisor     5    500000      300        0        320
tmr_svc        6    100000       20        0         20
U = 14.08 %  (Liu-Layland bound for n=6: 73.47 %)
RTA (budgeted WCET): PASS, all deadlines met
type 'help' for commands
>
```

After a watchdog or fault reset, the next boot also reports the cause, for example
`reset cause: watchdog (IWDG)` followed by `fault: WATCHDOG 'sensor' ...`.

## Highlights

| | |
|---|---|
| **Analysed real-time design** | 5 application tasks with rate-monotonic priorities. Exact response-time analysis (with blocking terms) runs at boot and on demand with *measured* WCETs from a 1 µs timer. See [docs/architecture.md](docs/architecture.md). |
| **Zero heap** | `configSUPPORT_DYNAMIC_ALLOCATION = 0`: every task, queue, mutex, event group, stream buffer and timer is static. The RAM budget is fixed at link time (18 KB of 96 KB). |
| **Watchdog that proves liveness** | Each task checks in through an event group. The supervisor feeds the 2 s IWDG only while every task is alive, so a deadlocked task resets the MCU even though the idle loop still runs. |
| **Post-mortem debugging** | HardFault, assert, stack-overflow and watchdog records (PC, LR, CFSR, HFSR, fault addresses) sit in CRC-protected `.noinit` RAM and are reported after the reset. |
| **Fault injection** | `crash hardfault \| assert \| stack \| stall` exercises every recovery path on real hardware. |
| **Correct ISR design** | Interrupt-driven UART over stream buffers, button handled through `xEventGroupSetBitsFromISR`. ISRs at NVIC 6–7 stay under `configMAX_SYSCALL_INTERRUPT_PRIORITY`, and 0–4 are left free for hard real-time work. |
| **Ratiometric measurements** | VDDA is computed every cycle from VREFINT and its factory calibration, so readings stay correct on a sagging supply. All filters and RMS are integer-only. |
| **Robust telemetry** | 24-byte frames with CRC-16, COBS framing and sequence numbers (to detect drops). The Python decoder shares a golden vector with the C tests. |
| **CI** | Host unit tests with ASan/UBSan (gcc + clang), cppcheck, cross-builds with stack-usage report, and a **QEMU boot test** that runs the shell and asserts on the UART log. Tagged releases publish the firmware. |

## Hardware

Only a NUCLEO-F401RE and a USB cable are needed. Everything else is optional.

| Signal | Pin | Notes |
|---|---|---|
| UART (CLI + telemetry) | PA2/PA3 → ST-LINK VCP | 115200 8N1 |
| Analog sensor | PA0 (Arduino **A0**) | 0–3.3 V: ADXL335 axis, ACS712 output via divider, potentiometer... |
| Status LED | LD2 (PA5) | heartbeat / double blink = latched alarm / 5 Hz = active alarm |
| Acknowledge | B1 (PC13) | acknowledges latched alarms |
| Die temperature, VREFINT | internal ADC1 IN18 / IN17 | |

## Build, flash, run

```sh
git clone --recursive https://github.com/MohansaiPotnuru/rtos-condition-monitor
cd rtos-condition-monitor
make                 # build/nucleo/rtos-condition-monitor.{elf,bin,hex,map}
make flash           # st-flash or STM32_Programmer_CLI
make test            # host unit tests (ASan + UBSan)
make qemu-run        # boot the QEMU build, UART on your terminal
```

Without a toolchain, download `rtos-condition-monitor.bin` from the
[latest release](https://github.com/MohansaiPotnuru/rtos-condition-monitor/releases/latest)
and drag it onto the `NODE_F401RE` USB drive. Then open the VCP at
115200 baud (PuTTY, `screen`, or `python -m serial.tools.miniterm COM5 115200`).

Binary telemetry:

```sh
pip install pyserial
python tools/tlm_decode.py COM5 --csv log.csv     # then type: tlm bin 100
```

## Shell

| Command | What it does |
|---|---|
| `status` | sensor values, alarms, CPU load, clock, boot count, reset cause |
| `ps` | per task: state, priority, stack high-water mark, CPU %, measured WCET |
| `rta [measured]` | response-time analysis with budgeted or measured WCETs |
| `tlm <off\|csv\|bin> [ms]` | telemetry mode and period (50–60000 ms) |
| `alarm [ack \| set <temp\|vdda\|rms> <set> <clear>]` | list, acknowledge or retune alarms (hysteresis is enforced) |
| `stats` | queue overflows and high water, deadline misses, UART errors, supervisor state |
| `fault [clear]` | last fault record (survives reset) |
| `selftest` | CRC/COBS/telemetry/RTA/stack/ADC self tests |
| `crash <hardfault\|assert\|stack\|stall>` | fault injection |
| `reset`, `version`, `help` | |

## Repository layout

```text
app/           tasks, shell commands, boot sequence
platform/      register-level BSP (no HAL): clocks, UART, ADC, IWDG, EXTI, startup, fault capture
lib/           hardware-independent modules, unit-tested on the host:
               crc16, cobs, sigstats (EMA, RMS, isqrt), alarm, adc_conv, rta, health, cli, telemetry
config/        FreeRTOSConfig.h
tests/         host unit tests (24 000+ checks incl. randomised COBS round trips)
tools/         telemetry decoder, its contract tests, QEMU log checker
third_party/   FreeRTOS-Kernel V11.1.0 (git submodule, MIT)
vendor/cmsis/  CMSIS core + STM32F4 device headers (Apache-2.0)
```

## Verification status

| Level | Status |
|---|---|
| Host unit tests (lib/) | 23 suites, 24 000+ checks, ASan + UBSan, gcc and clang |
| Cross-build, both targets | zero warnings with `-Wall -Wextra -Werror` on project code |
| QEMU boot (netduinoplus2, Cortex-M4) | scheduler, all tasks, shell, RTA, self tests and telemetry checked on every push |
| NUCLEO-F401RE hardware | clock tree, ADC, IWDG and GPIO paths are written against RM0368; on-board validation is the next step |

QEMU does not model the STM32 RCC, GPIO, IWDG or the ADC common block, so the
QEMU build skips those registers and transmits by polling. Everything above the
BSP is the same code.

## License

MIT, see [LICENSE](LICENSE). FreeRTOS-Kernel is MIT; the CMSIS headers are Apache-2.0.

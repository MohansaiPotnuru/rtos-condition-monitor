/**
 * @file    app_cli.c
 * @brief   Shell commands: introspection, schedulability, telemetry, alarms,
 *          fault records and fault injection.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "app.h"
#include "bsp.h"
#include "cli.h"
#include "cobs.h"
#include "crc16.h"
#include "fault.h"
#include "rta.h"
#include "task.h"

#define P app_printf

static bool parse_i32(const char *s, int32_t *out)
{
    char *end = NULL;
    long  v   = strtol(s, &end, 0);
    if (end == s || *end != '\0' || v < -2000000000L || v > 2000000000L) {
        return false;
    }
    *out = (int32_t)v;
    return true;
}

/* ------------------------------------------------------------- version --- */

static int cmd_version(int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    P("%s %s on %s, FreeRTOS %s, built %s %s\r\n", APP_NAME, APP_VERSION, BSP_BOARD_NAME, tskKERNEL_VERSION_NUMBER,
      __DATE__, __TIME__);
    return CLI_OK;
}

/* -------------------------------------------------------------- status --- */

static void print_temp(const char *label, int32_t cdeg)
{
    const int32_t m = (cdeg < 0) ? -cdeg : cdeg;
    P("%s%s%ld.%02ld C\r\n", label, (cdeg < 0) ? "-" : "", (long)(m / 100), (long)(m % 100));
}

static int cmd_status(int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    tlm_snapshot_t s;
    app_snapshot_get(&s);
    const uint32_t up = (uint32_t)xTaskGetTickCount();

    P("uptime    : %lu.%03lu s   boot #%lu, last reset: %s\r\n", (unsigned long)(up / 1000u),
      (unsigned long)(up % 1000u), (unsigned long)fault_boot_count(), bsp_reset_cause_str(bsp_reset_cause()));
    P("clock     : %lu MHz%s   cpu load %u %%\r\n", (unsigned long)(SystemCoreClock / 1000000u),
      bsp_clock_fallback() ? " (PLL FAILED, on HSI)" : "", (unsigned)s.cpu_pct);
    print_temp("die temp  : ", s.temp_cdeg);
    P("vdda      : %u mV\r\n", (unsigned)s.vdda_mv);
    P("ain (PA0) : mean %u mV  min %u  max %u  ac-rms %u mV (1 s window)\r\n", (unsigned)s.ain_mv,
      (unsigned)s.ain_min_mv, (unsigned)s.ain_max_mv, (unsigned)s.ain_rms_mv);
    P("alarms    : active 0x%X  latched 0x%X  (1=TEMP 2=VDDA 4=RMS)\r\n", (unsigned)(s.alarms & 0x0Fu),
      (unsigned)(s.alarms >> TLM_LATCH_SHIFT));
    static const char *const mode[] = {"off", "csv", "bin"};
    P("telemetry : %s every %lu ms\r\n", mode[app_tlm_mode()], (unsigned long)app_tlm_period_ms());
    return CLI_OK;
}

/* ------------------------------------------------------------------ ps --- */

static char state_char(eTaskState s)
{
    switch (s) {
    case eRunning:   return 'X';
    case eReady:     return 'R';
    case eBlocked:   return 'B';
    case eSuspended: return 'S';
    case eDeleted:   return 'D';
    default:         return '?';
    }
}

static int cmd_ps(int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    static TaskStatus_t st[12]; /* static: keeps the CLI stack small */
    uint32_t            total = 0u;
    const UBaseType_t   n     = uxTaskGetSystemState(st, 12, &total);

    P("%-11s %s %4s %9s %6s %9s %9s\r\n", "task", "st", "prio", "stack-free", "cpu%", "wcet-us", "runs");
    for (UBaseType_t i = 0; i < n; i++) {
        const uint32_t pct10 = (total > 0u) ? (uint32_t)(((uint64_t)st[i].ulRunTimeCounter * 1000u) / total) : 0u;
        uint32_t       wcet = 0u, runs = 0u;
        for (unsigned t = 0u; t < TASK_COUNT; t++) {
            if (strcmp(st[i].pcTaskName, g_task_spec[t].name) == 0) {
                wcet = g_exec[t].max_us;
                runs = g_exec[t].runs;
            }
        }
        P("%-11s  %c %4u %6u w %4lu.%lu %9lu %9lu\r\n", st[i].pcTaskName, state_char(st[i].eCurrentState),
          (unsigned)st[i].uxCurrentPriority, (unsigned)st[i].usStackHighWaterMark, (unsigned long)(pct10 / 10u),
          (unsigned long)(pct10 % 10u), (unsigned long)wcet, (unsigned long)runs);
    }
    P("(stack-free = lowest free stack ever, in 32-bit words; cpu%% since boot)\r\n");
    return CLI_OK;
}

/* ----------------------------------------------------------------- rta --- */

static bool print_rta(bool measured)
{
    rta_task_t   t[TASK_COUNT + 1];
    rta_result_t r[TASK_COUNT + 1];
    const size_t n  = app_rta_table(t, TASK_COUNT + 1, measured);
    const bool   ok = rta_analyse(t, n, r);
    const uint32_t u  = rta_utilization_ppm(t, n);
    const uint32_t ll = rta_ll_bound_ppm(n);

    P("%-11s %4s %9s %8s %8s %10s\r\n", "task", "prio", "T(us)", "C(us)", "B(us)", "R(us)");
    for (size_t i = 0; i < n; i++) {
        if (r[i].schedulable) {
            P("%-11s %4u %9lu %8lu %8lu %10lu\r\n", t[i].name, (unsigned)t[i].priority, (unsigned long)t[i].period_us,
              (unsigned long)t[i].wcet_us, (unsigned long)t[i].blocking_us, (unsigned long)r[i].response_us);
        } else {
            P("%-11s %4u %9lu %8lu %8lu %10s\r\n", t[i].name, (unsigned)t[i].priority, (unsigned long)t[i].period_us,
              (unsigned long)t[i].wcet_us, (unsigned long)t[i].blocking_us, "MISS");
        }
    }
    P("U = %lu.%02lu %%  (Liu-Layland bound for n=%u: %lu.%02lu %%)\r\n", (unsigned long)(u / 10000u),
      (unsigned long)((u / 100u) % 100u), (unsigned)n, (unsigned long)(ll / 10000u),
      (unsigned long)((ll / 100u) % 100u));
    P("RTA (%s WCET): %s\r\n", measured ? "measured" : "budgeted", ok ? "PASS, all deadlines met" : "FAIL");
    return ok;
}

static int cmd_rta(int argc, char **argv, void *ctx)
{
    (void)ctx;
    const bool measured = (argc > 1 && strcmp(argv[1], "measured") == 0);
    if (argc > 1 && !measured) {
        return CLI_ERR_ARGS;
    }
    return print_rta(measured) ? CLI_OK : CLI_ERR_FAILED;
}

/* ----------------------------------------------------------------- tlm --- */

static int cmd_tlm(int argc, char **argv, void *ctx)
{
    (void)ctx;
    if (argc < 2 || argc > 3) {
        return CLI_ERR_ARGS;
    }
    tlm_mode_t m;
    if (strcmp(argv[1], "off") == 0) {
        m = TLM_MODE_OFF;
    } else if (strcmp(argv[1], "csv") == 0) {
        m = TLM_MODE_CSV;
    } else if (strcmp(argv[1], "bin") == 0) {
        m = TLM_MODE_BIN;
    } else {
        return CLI_ERR_ARGS;
    }
    int32_t period = 0;
    if (argc == 3 && (!parse_i32(argv[2], &period) || period < 50 || period > 60000)) {
        P("period must be 50..60000 ms\r\n");
        return CLI_ERR_ARGS;
    }
    app_tlm_set(m, (uint32_t)period);
    P("telemetry %s, every %lu ms\r\n", argv[1], (unsigned long)app_tlm_period_ms());
    return CLI_OK;
}

/* --------------------------------------------------------------- alarm --- */

static int cmd_alarm(int argc, char **argv, void *ctx)
{
    (void)ctx;
    static const char *const names[ALARM_ID_COUNT] = {"temp", "vdda", "rms"};
    static const char *const units[ALARM_ID_COUNT] = {"cdegC", "mV", "mV"};

    if (argc == 1) {
        alarm_t a[ALARM_ID_COUNT];
        app_alarms_get(a);
        for (unsigned i = 0u; i < ALARM_ID_COUNT; i++) {
            P("%-5s %s set %ld clear %ld %s  %s%s trips %lu\r\n", names[i], a[i].cfg.dir == ALARM_HIGH ? ">=" : "<=",
              (long)a[i].cfg.set, (long)a[i].cfg.clear, units[i], a[i].active ? "ACTIVE " : "",
              a[i].latched ? "LATCHED " : "", (unsigned long)a[i].trips);
        }
        return CLI_OK;
    }
    if (argc == 2 && strcmp(argv[1], "ack") == 0) {
        app_alarms_ack();
        return CLI_OK;
    }
    if (argc == 5 && strcmp(argv[1], "set") == 0) {
        int32_t set, clr;
        for (unsigned i = 0u; i < ALARM_ID_COUNT; i++) {
            if (strcmp(argv[2], names[i]) == 0 && parse_i32(argv[3], &set) && parse_i32(argv[4], &clr)) {
                if (app_alarm_configure((alarm_id_t)i, set, clr)) {
                    P("%s: set %ld clear %ld\r\n", names[i], (long)set, (long)clr);
                    return CLI_OK;
                }
                P("rejected: needs a hysteresis gap in the alarm direction\r\n");
                return CLI_ERR_FAILED;
            }
        }
    }
    return CLI_ERR_ARGS;
}

/* --------------------------------------------------------------- stats --- */

static int cmd_stats(int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    app_counters_t   c;
    bsp_uart_stats_t u;
    app_counters_get(&c);
    bsp_uart_get_stats(&u);
    const health_t *h = app_health();

    P("samples %lu  windows %lu  adc errors %lu  telemetry frames %lu\r\n", (unsigned long)c.samples,
      (unsigned long)c.windows, (unsigned long)c.adc_errors, (unsigned long)c.tlm_frames);
    P("sample queue: overflows %lu, high water %u / 16\r\n", (unsigned long)c.queue_overflows,
      (unsigned)c.queue_high_water);
    P("deadline misses: sensor %lu, telemetry %lu\r\n", (unsigned long)g_exec[TASK_SENSOR].deadline_misses,
      (unsigned long)g_exec[TASK_TELEMETRY].deadline_misses);
    P("uart: rx overrun %lu, rx dropped %lu, tx dropped %lu\r\n", (unsigned long)u.rx_overrun,
      (unsigned long)u.rx_dropped, (unsigned long)u.tx_dropped);
    P("supervisor: %lu windows, last missing 0x%lX, %s\r\n", (unsigned long)h->windows,
      (unsigned long)h->last_missing, h->tripped ? "TRIPPED" : "feeding IWDG");
    return CLI_OK;
}

/* --------------------------------------------------------------- fault --- */

static void puts_async(const char *s)
{
    app_print(s);
}

static int cmd_fault(int argc, char **argv, void *ctx)
{
    (void)ctx;
    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        fault_clear();
        P("fault record cleared\r\n");
        return CLI_OK;
    }
    if (argc != 1) {
        return CLI_ERR_ARGS;
    }
    fault_record_t r;
    if (!fault_get(&r)) {
        P("no fault recorded\r\n");
        return CLI_OK;
    }
    fault_print(&r, puts_async);
    return CLI_OK;
}

/* ------------------------------------------------------------ selftest --- */

static bool check(const char *what, bool ok)
{
    P("  %-34s %s\r\n", what, ok ? "ok" : "FAIL");
    return ok;
}

static int cmd_selftest(int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    bool ok = true;

    ok &= check("crc16 check value 0x29B1", crc16_ccitt("123456789", 9, CRC16_INIT) == 0x29B1u);

    static const uint8_t data[] = {0x11, 0x00, 0x00, 0x22, 0x00};
    uint8_t              enc[COBS_MAX_ENCODED(sizeof data)], dec[sizeof data];
    size_t               dn = 0;
    const size_t         en = cobs_encode(data, sizeof data, enc, sizeof enc);
    ok &= check("cobs round trip", en > 0 && cobs_decode(enc, en, dec, sizeof dec, &dn) && dn == sizeof data &&
                                       memcmp(dec, data, dn) == 0);

    tlm_snapshot_t s, back;
    app_snapshot_get(&s);
    uint8_t  raw[TLM_RAW_LEN];
    uint16_t seq = 0;
    ok &= check("telemetry pack/unpack + crc",
                tlm_pack(&s, 77, 1234, raw, sizeof raw) == TLM_RAW_LEN && tlm_unpack(raw, sizeof raw, &back, &seq, NULL) &&
                    seq == 77 && back.vdda_mv == s.vdda_mv);

    rta_task_t t[TASK_COUNT + 1];
    const size_t n = app_rta_table(t, TASK_COUNT + 1, false);
    ok &= check("task set schedulable (RTA)", rta_analyse(t, n, NULL));

    UBaseType_t min_free = 0xFFFF;
    for (unsigned i = 0u; i < TASK_COUNT; i++) {
        TaskHandle_t h = xTaskGetHandle(g_task_spec[i].name);
        if (h != NULL) {
            const UBaseType_t f = uxTaskGetStackHighWaterMark(h);
            min_free            = (f < min_free) ? f : min_free;
        }
    }
    ok &= check("every task has >= 32 words stack free", min_free >= 32u);

    app_counters_t c;
    app_counters_get(&c);
    ok &= check("sampling running", c.samples > 0u);
#if !defined(TARGET_QEMU)
    tlm_snapshot_t now;
    app_snapshot_get(&now);
    ok &= check("vdda plausible (1.8 .. 3.6 V)", now.vdda_mv >= 1800u && now.vdda_mv <= 3600u);
    ok &= check("adc conversions error-free", c.adc_errors == 0u);
#endif

    P("selftest: %s\r\n", ok ? "PASS" : "FAIL");
    return ok ? CLI_OK : CLI_ERR_FAILED;
}

/* --------------------------------------------------------------- crash --- */

static volatile uint32_t s_sink;

__attribute__((noinline)) static uint32_t burn_stack(uint32_t depth)
{
    volatile uint8_t buf[128];
    buf[0] = (uint8_t)depth;
    s_sink += buf[0];
    return (depth == 0u) ? buf[0] : burn_stack(depth - 1u) + buf[0];
}

static int cmd_crash(int argc, char **argv, void *ctx)
{
    (void)ctx;
    if (argc != 2) {
        return CLI_ERR_ARGS;
    }
    if (strcmp(argv[1], "hardfault") == 0) {
        P("executing an undefined instruction...\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        __builtin_trap();
    } else if (strcmp(argv[1], "assert") == 0) {
        P("failing a configASSERT...\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        configASSERT(s_sink == 0xDEADBEEFu);
    } else if (strcmp(argv[1], "stack") == 0) {
        P("recursing until the CLI stack overflows...\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        s_sink = burn_stack(64);
        taskYIELD(); /* overflow is checked on the next context switch */
    } else if (strcmp(argv[1], "stall") == 0) {
        P("sensor task stops checking in; supervisor will starve the IWDG (~1.5 s + 2 s)\r\n");
        app_inject_stall(TASK_SENSOR);
    } else {
        return CLI_ERR_ARGS;
    }
    return CLI_OK;
}

static int cmd_reset(int argc, char **argv, void *ctx)
{
    (void)argc;
    (void)argv;
    (void)ctx;
    P("resetting\r\n");
    vTaskDelay(pdMS_TO_TICKS(50));
    bsp_system_reset();
    return CLI_OK;
}

/* ------------------------------------------------------------ the shell --- */

static const cli_cmd_t k_cmds[] = {
    {"version", "", "firmware, board and kernel version", cmd_version},
    {"status", "", "sensor values, alarms, clock, reset cause", cmd_status},
    {"ps", "", "tasks: state, priority, stack high-water, CPU, WCET", cmd_ps},
    {"rta", "[measured]", "response-time analysis of the task set", cmd_rta},
    {"tlm", "<off|csv|bin> [period_ms]", "telemetry mode and period", cmd_tlm},
    {"alarm", "[ack | set <temp|vdda|rms> <set> <clear>]", "list, acknowledge or tune alarms", cmd_alarm},
    {"stats", "", "counters: queues, deadlines, UART, supervisor", cmd_stats},
    {"fault", "[clear]", "show the last fault record (survives reset)", cmd_fault},
    {"selftest", "", "run built-in self tests", cmd_selftest},
    {"crash", "<hardfault|assert|stack|stall>", "inject a fault to test recovery", cmd_crash},
    {"reset", "", "software reset", cmd_reset},
};

static void cli_out(const char *s, void *ctx)
{
    (void)ctx;
    app_print(s);
}

void app_cli_task(void *arg)
{
    (void)arg;
    static cli_t cli; /* static: the line buffer does not need to live on the stack */
    cli_init(&cli, k_cmds, sizeof k_cmds / sizeof k_cmds[0], cli_out, NULL, "> ", true);

    vTaskDelay(pdMS_TO_TICKS(300)); /* let the first sensor window fill */
    {
        fault_record_t r;
        if (fault_get(&r)) {
            app_print("previous run ended with a fault (see 'fault', clear with 'fault clear'):\r\n");
            fault_print(&r, puts_async);
        }
    }
    (void)print_rta(false);

#if defined(TARGET_QEMU)
    /* CI smoke test: exercise the shell without a terminal attached. */
    static const char *const script[] = {"version", "selftest", "ps", "stats", "alarm", "tlm csv 200"};
    for (size_t i = 0; i < sizeof script / sizeof script[0]; i++) {
        char line[CLI_LINE_MAX];
        snprintf(line, sizeof line, "%s", script[i]);
        P("> %s\r\n", line);
        (void)cli_execute(&cli, line);
        app_checkin(TASK_CLI);
    }
#endif
    app_print("type 'help' for commands\r\n> ");

    for (;;) {
        uint8_t      buf[16];
        const size_t n = bsp_uart_read(buf, sizeof buf, 100u);
        for (size_t i = 0; i < n; i++) {
            cli_feed(&cli, (char)buf[i]);
        }
        app_checkin(TASK_CLI);
    }
}

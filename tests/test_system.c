/* rta, health, cli and telemetry. */
#include <stdint.h>
#include <stdio.h>

#include "cli.h"
#include "health.h"
#include "rta.h"
#include "telemetry.h"
#include "unit.h"

/* ---------------------------------------------------------------- RTA ---- */

void test_rta_textbook_example(void)
{
    /* Burns & Wellings, "Real-Time Systems", task set with C/T = 3/7, 3/12, 5/20.
     * Known response times: 3, 6, 20 (the lowest-priority task just makes it). */
    const rta_task_t ts[] = {
        {"a", 7, 3, 0, 0, 3},
        {"b", 12, 3, 0, 0, 2},
        {"c", 20, 5, 0, 0, 1},
    };
    rta_result_t r[3];
    CHECK(rta_analyse(ts, 3, r));
    CHECK_EQ(r[0].response_us, 3);
    CHECK_EQ(r[1].response_us, 6);
    CHECK_EQ(r[2].response_us, 20);
    /* U = 0.4286 + 0.25 + 0.25 = 0.9286 > LL bound 0.7798: the bound is only sufficient */
    CHECK(rta_utilization_ppm(ts, 3) > rta_ll_bound_ppm(3));
}

void test_rta_detects_overload_and_blocking(void)
{
    rta_task_t ts[] = {
        {"a", 7, 3, 0, 0, 3},
        {"b", 12, 3, 0, 0, 2},
        {"c", 20, 6, 0, 0, 1}, /* one more unit of work breaks task c */
    };
    rta_result_t r[3];
    CHECK(!rta_analyse(ts, 3, r));
    CHECK(r[0].schedulable && r[1].schedulable);
    CHECK(!r[2].schedulable);
    CHECK_EQ(r[2].response_us, UINT32_MAX);

    /* Blocking: a lower-priority task holds a mutex for 2 units */
    ts[2].wcet_us     = 5;
    ts[0].blocking_us = 2;
    CHECK(rta_analyse(ts, 3, r));
    CHECK_EQ(r[0].response_us, 5);

    /* Constrained deadline tighter than the response time */
    ts[0].deadline_us = 4;
    CHECK(!rta_analyse(ts, 3, r));
}

void test_rta_equal_priorities_interfere(void)
{
    const rta_task_t ts[] = {
        {"x", 10, 4, 0, 0, 1},
        {"y", 10, 4, 0, 0, 1},
    };
    rta_result_t r[2];
    CHECK(rta_analyse(ts, 2, r));
    CHECK_EQ(r[0].response_us, 8);
    CHECK_EQ(r[1].response_us, 8);
}

void test_rta_ll_bound_table(void)
{
    CHECK_EQ(rta_ll_bound_ppm(1), 1000000);
    CHECK_EQ(rta_ll_bound_ppm(2), 828427);
    CHECK_EQ(rta_ll_bound_ppm(5), 743492);
    CHECK_EQ(rta_ll_bound_ppm(50), 693147);
    const rta_task_t ts[] = {{"a", 1000, 250, 0, 0, 1}, {"b", 100, 10, 0, 0, 2}};
    CHECK_EQ(rta_utilization_ppm(ts, 2), 350000);
}

/* ------------------------------------------------------------- health ---- */

void test_health_tolerates_single_miss(void)
{
    health_t h;
    health_init(&h, 0x0F, 1); /* 4 tasks, one missed window tolerated */
    CHECK(health_evaluate(&h, 0x0F));
    CHECK(health_evaluate(&h, 0x0B)); /* task 2 late once */
    CHECK_EQ(h.last_missing, 0x04);
    CHECK(health_evaluate(&h, 0x0F)); /* recovers */
    CHECK_EQ(h.consecutive[2], 0);
    CHECK_EQ(h.total_misses[2], 1);
    CHECK(!h.tripped);
}

void test_health_trips_on_stall_and_stays_tripped(void)
{
    health_t h;
    health_init(&h, 0x07, 2);
    CHECK(health_evaluate(&h, 0x05)); /* task 1 missing: 1 */
    CHECK(health_evaluate(&h, 0x05)); /* 2 */
    CHECK(!health_evaluate(&h, 0x05)); /* 3 > 2: stop feeding */
    CHECK(h.tripped);
    CHECK_EQ(h.culprit, 1);
    CHECK(!health_evaluate(&h, 0x07)); /* sticky, even after recovery */
    /* Unmonitored bits are ignored */
    health_init(&h, 0x01, 0);
    CHECK(health_evaluate(&h, 0x01 | 0x80));
    CHECK(!health_evaluate(&h, 0x80));
}

/* ---------------------------------------------------------------- CLI ---- */

static char g_out[2048];
static int  g_last_argc;
static char g_last_arg1[32];

static void cap_write(const char *s, void *ctx)
{
    (void)ctx;
    strncat(g_out, s, sizeof g_out - strlen(g_out) - 1);
}

static int cmd_echo(int argc, char **argv, void *ctx)
{
    (void)ctx;
    g_last_argc = argc;
    g_last_arg1[0] = '\0';
    if (argc > 1) {
        snprintf(g_last_arg1, sizeof g_last_arg1, "%s", argv[1]);
    }
    return CLI_OK;
}

static int cmd_needs_arg(int argc, char **argv, void *ctx)
{
    (void)argv;
    (void)ctx;
    return (argc == 2) ? CLI_OK : CLI_ERR_ARGS;
}

static const cli_cmd_t k_cmds[] = {
    {"echo", "[text]", "print arguments", cmd_echo},
    {"need", "<x>", "needs one argument", cmd_needs_arg},
};

static void feed(cli_t *c, const char *s)
{
    while (*s) {
        cli_feed(c, *s++);
    }
}

void test_cli_tokenize(void)
{
    char  line[] = "  set   \"two words\"\tx  ";
    char *argv[8];
    CHECK_EQ(cli_tokenize(line, argv, 8), 3);
    CHECK_STR(argv[0], "set");
    CHECK_STR(argv[1], "two words");
    CHECK_STR(argv[2], "x");

    char bad[] = "a \"open";
    CHECK_EQ(cli_tokenize(bad, argv, 8), -1);

    char many[] = "1 2 3 4 5";
    CHECK_EQ(cli_tokenize(many, argv, 3), 3);
}

void test_cli_dispatch_and_line_endings(void)
{
    cli_t c;
    cli_init(&c, k_cmds, 2, cap_write, NULL, "> ", false);

    g_out[0] = '\0';
    feed(&c, "echo hello\r\n"); /* CRLF must run exactly once */
    CHECK_EQ(g_last_argc, 2);
    CHECK_STR(g_last_arg1, "hello");
    CHECK_STR(g_out, "> ");

    g_last_argc = 0;
    feed(&c, "echo a b\n");
    CHECK_EQ(g_last_argc, 3);

    g_out[0] = '\0';
    feed(&c, "bogus\r");
    CHECK(strstr(g_out, "unknown command 'bogus'") != NULL);

    g_out[0] = '\0';
    feed(&c, "need\r");
    CHECK(strstr(g_out, "usage: need <x>") != NULL);

    g_out[0] = '\0';
    feed(&c, "help\r");
    CHECK(strstr(g_out, "echo [text]") != NULL);
    CHECK(strstr(g_out, "needs one argument") != NULL);

    g_out[0] = '\0';
    feed(&c, "\r");
    CHECK_STR(g_out, "> "); /* empty line: just a new prompt */
}

void test_cli_editing_and_overflow(void)
{
    cli_t c;
    cli_init(&c, k_cmds, 2, cap_write, NULL, NULL, true);

    g_out[0] = '\0';
    feed(&c, "echo hellx\bo\r");
    CHECK_STR(g_last_arg1, "hello");
    CHECK(strstr(g_out, "\b \b") != NULL); /* echo erases the character */

    g_out[0] = '\0';
    for (unsigned i = 0; i < CLI_LINE_MAX + 20; i++) {
        cli_feed(&c, 'x');
    }
    cli_feed(&c, '\r');
    CHECK(strstr(g_out, "line too long") != NULL);

    /* The shell recovers on the next line */
    g_last_argc = 0;
    feed(&c, "echo ok\r");
    CHECK_STR(g_last_arg1, "ok");

    /* Control and non-ASCII bytes are ignored */
    feed(&c, "echo \x01q\xC3\r");
    CHECK_STR(g_last_arg1, "q");
}

/* ---------------------------------------------------------- telemetry ---- */

/* Golden vector shared with tools/test_tlm_decode.py. Do not change one without the other. */
static const tlm_snapshot_t k_golden = {
    .temp_cdeg = -1234, .vdda_mv = 3298, .ain_mv = 1650, .ain_min_mv = 1200,
    .ain_max_mv = 2100, .ain_rms_mv = 318, .alarms = 0x13, .cpu_pct = 7,
};
static const uint8_t k_golden_wire[] = {
    0x08, 0x01, 0x01, 0x34, 0x12, 0x4E, 0x61, 0xBC, 0x11, 0x2E, 0xFB, 0xE2, 0x0C,
    0x72, 0x06, 0xB0, 0x04, 0x34, 0x08, 0x3E, 0x01, 0x13, 0x07, 0x87, 0x66, 0x00,
};

void test_telemetry_golden_and_roundtrip(void)
{
    uint8_t wire[TLM_WIRE_MAX];
    const size_t n = tlm_encode_wire(&k_golden, 0x1234, 0x00BC614Eu, wire, sizeof wire);

    if (n != sizeof k_golden_wire || memcmp(wire, k_golden_wire, n) != 0) {
        printf("  wire (%u bytes):", (unsigned)n);
        for (size_t i = 0; i < n; i++) {
            printf(" %02X", wire[i]);
        }
        printf("\n");
    }
    CHECK_EQ(n, sizeof k_golden_wire);
    CHECK_MEM(wire, k_golden_wire, sizeof k_golden_wire);

    uint8_t raw[TLM_RAW_LEN];
    CHECK_EQ(tlm_pack(&k_golden, 7, 42, raw, sizeof raw), TLM_RAW_LEN);
    tlm_snapshot_t s;
    uint16_t       seq = 0;
    uint32_t       up  = 0;
    CHECK(tlm_unpack(raw, sizeof raw, &s, &seq, &up));
    CHECK_EQ(seq, 7);
    CHECK_EQ(up, 42);
    CHECK_EQ(s.temp_cdeg, -1234);
    CHECK_EQ(s.ain_rms_mv, 318);
    CHECK_EQ(s.alarms, 0x13);

    raw[9] ^= 0x01; /* single bit flip must be caught by the CRC */
    CHECK(!tlm_unpack(raw, sizeof raw, &s, &seq, &up));
    CHECK(!tlm_unpack(raw, sizeof raw - 1, &s, &seq, &up));
    CHECK_EQ(tlm_pack(&k_golden, 0, 0, raw, sizeof raw - 1), 0);
}

void test_telemetry_csv(void)
{
    char buf[160];
    const int n = tlm_format_csv(&k_golden, 5, 1000, buf, sizeof buf);
    CHECK(n > 0);
    CHECK_STR(buf, "tlm,5,1000,-12.34,3298,1650,1200,2100,318,0x13,7\r\n");

    tlm_snapshot_t s = k_golden;
    s.temp_cdeg      = 5;
    tlm_format_csv(&s, 0, 0, buf, sizeof buf);
    CHECK(strncmp(buf, "tlm,0,0,0.05,", 13) == 0);
    s.temp_cdeg = -5;
    tlm_format_csv(&s, 0, 0, buf, sizeof buf);
    CHECK(strncmp(buf, "tlm,0,0,-0.05,", 14) == 0);

    CHECK_EQ(tlm_format_csv(&k_golden, 5, 1000, buf, 10), -1);
    CHECK(strncmp(tlm_csv_header(), "#tlm,seq,", 9) == 0);
}

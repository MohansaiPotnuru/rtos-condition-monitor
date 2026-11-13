#include "unit.h"

int g_checks;
int g_failures;

void test_crc16_check_value(void);
void test_cobs_wikipedia_vectors(void);
void test_cobs_block_boundaries(void);
void test_cobs_rejects_bad_input(void);
void test_cobs_random_roundtrip(void);
void test_isqrt_exact(void);
void test_ema_converges_and_rounds(void);
void test_window_stats_sine(void);
void test_window_stats_dc_and_reset(void);
void test_alarm_hysteresis_and_debounce(void);
void test_alarm_low_and_validation(void);
void test_adc_conversions(void);
void test_rta_textbook_example(void);
void test_rta_detects_overload_and_blocking(void);
void test_rta_equal_priorities_interfere(void);
void test_rta_ll_bound_table(void);
void test_health_tolerates_single_miss(void);
void test_health_trips_on_stall_and_stays_tripped(void);
void test_cli_tokenize(void);
void test_cli_dispatch_and_line_endings(void);
void test_cli_editing_and_overflow(void);
void test_telemetry_golden_and_roundtrip(void);
void test_telemetry_csv(void);

int main(void)
{
    RUN(test_crc16_check_value);
    RUN(test_cobs_wikipedia_vectors);
    RUN(test_cobs_block_boundaries);
    RUN(test_cobs_rejects_bad_input);
    RUN(test_cobs_random_roundtrip);
    RUN(test_isqrt_exact);
    RUN(test_ema_converges_and_rounds);
    RUN(test_window_stats_sine);
    RUN(test_window_stats_dc_and_reset);
    RUN(test_alarm_hysteresis_and_debounce);
    RUN(test_alarm_low_and_validation);
    RUN(test_adc_conversions);
    RUN(test_rta_textbook_example);
    RUN(test_rta_detects_overload_and_blocking);
    RUN(test_rta_equal_priorities_interfere);
    RUN(test_rta_ll_bound_table);
    RUN(test_health_tolerates_single_miss);
    RUN(test_health_trips_on_stall_and_stays_tripped);
    RUN(test_cli_tokenize);
    RUN(test_cli_dispatch_and_line_endings);
    RUN(test_cli_editing_and_overflow);
    RUN(test_telemetry_golden_and_roundtrip);
    RUN(test_telemetry_csv);

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

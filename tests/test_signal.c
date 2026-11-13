/* sigstats, alarm and adc_conv. */
#include <math.h>
#include <stdint.h>

#include "adc_conv.h"
#include "alarm.h"
#include "sigstats.h"
#include "unit.h"

void test_isqrt_exact(void)
{
    CHECK_EQ(isqrt32(0), 0);
    CHECK_EQ(isqrt32(1), 1);
    CHECK_EQ(isqrt32(15), 3);
    CHECK_EQ(isqrt32(16), 4);
    CHECK_EQ(isqrt32(UINT32_MAX), 65535);
    CHECK_EQ(isqrt64(UINT64_MAX), 4294967295u);
    CHECK_EQ(isqrt64(1000000000000ull), 1000000);
    for (uint64_t r = 1; r < 200000; r += 37) {
        CHECK_EQ(isqrt64(r * r), r);
        CHECK_EQ(isqrt64(r * r - 1), r - 1);
        CHECK_EQ(isqrt64(r * r + 2 * r), r); /* (r+1)^2 - 1 */
    }
}

void test_ema_converges_and_rounds(void)
{
    ema_t f;
    ema_init(&f, 3);
    CHECK_EQ(ema_update(&f, 1000), 1000); /* first sample primes */
    int32_t v = 0;
    for (int i = 0; i < 200; i++) {
        v = ema_update(&f, 2000);
    }
    CHECK_EQ(v, 2000);
    for (int i = 0; i < 200; i++) {
        v = ema_update(&f, -1500);
    }
    CHECK_EQ(v, -1500);

    /* Step response after one sample with alpha = 1/8: 0 -> 800 * 1/8 = 100 */
    ema_init(&f, 3);
    ema_update(&f, 0);
    CHECK_EQ(ema_update(&f, 800), 100);

    ema_init(&f, 0); /* shift is clamped to 1..15 */
    CHECK_EQ(f.shift, 1);
}

void test_window_stats_sine(void)
{
    win_stats_t  w;
    win_result_t r;
    win_init(&w, 1000);

    /* 2048 +/- 1000 sine, 10 full periods: AC RMS = 1000/sqrt(2) = 707.1 */
    const double pi = 3.14159265358979323846;
    int          completed = 0;
    for (int i = 0; i < 1000; i++) {
        const uint16_t x = (uint16_t)lround(2048.0 + 1000.0 * sin(2.0 * pi * 10.0 * i / 1000.0));
        if (win_add(&w, x, &r)) {
            completed++;
        }
    }
    CHECK_EQ(completed, 1);
    CHECK_EQ(r.n, 1000);
    CHECK(r.mean >= 2047 && r.mean <= 2049);
    CHECK(r.ac_rms >= 706 && r.ac_rms <= 708);
    CHECK_EQ(r.min, 1048);
    CHECK_EQ(r.max, 3048);
}

void test_window_stats_dc_and_reset(void)
{
    win_stats_t  w;
    win_result_t r;
    win_init(&w, 4);
    CHECK(!win_add(&w, 100, &r));
    CHECK(!win_add(&w, 100, &r));
    CHECK(!win_add(&w, 100, &r));
    CHECK(win_add(&w, 100, &r));
    CHECK_EQ(r.ac_rms, 0);
    CHECK_EQ(r.mean, 100);
    /* Next window starts clean */
    win_add(&w, 0, &r);
    win_add(&w, 4095, &r);
    win_add(&w, 0, &r);
    CHECK(win_add(&w, 4095, &r));
    CHECK_EQ(r.min, 0);
    CHECK_EQ(r.max, 4095);
    CHECK(r.ac_rms >= 2047 && r.ac_rms <= 2048); /* square wave: amplitude 2047.5 */

    win_init(&w, 0);
    CHECK_EQ(w.size, 1);
    win_init(&w, 60000);
    CHECK_EQ(w.size, 4096);
}

void test_alarm_hysteresis_and_debounce(void)
{
    alarm_t           a;
    const alarm_cfg_t c = {.set = 6000, .clear = 5500, .set_count = 3, .clear_count = 2, .dir = ALARM_HIGH};
    CHECK(alarm_init(&a, &c));

    /* Two high samples then a dip: the count restarts, no alarm */
    CHECK_EQ(alarm_update(&a, 6100), ALARM_EV_NONE);
    CHECK_EQ(alarm_update(&a, 6100), ALARM_EV_NONE);
    CHECK_EQ(alarm_update(&a, 5900), ALARM_EV_NONE);
    CHECK(!a.active);

    CHECK_EQ(alarm_update(&a, 6000), ALARM_EV_NONE);
    CHECK_EQ(alarm_update(&a, 6050), ALARM_EV_NONE);
    CHECK_EQ(alarm_update(&a, 6400), ALARM_EV_RAISED);
    CHECK(a.active && a.latched);
    CHECK_EQ(a.trips, 1);

    /* Inside the hysteresis band: stays active */
    for (int i = 0; i < 10; i++) {
        CHECK_EQ(alarm_update(&a, 5800), ALARM_EV_NONE);
    }
    CHECK(a.active);
    CHECK_EQ(a.peak, 6400);
    CHECK(!alarm_ack(&a)); /* cannot ack while active */

    CHECK_EQ(alarm_update(&a, 5500), ALARM_EV_NONE);
    CHECK_EQ(alarm_update(&a, 5400), ALARM_EV_CLEARED);
    CHECK(!a.active && a.latched);
    CHECK(alarm_ack(&a));
    CHECK(!a.latched);
}

void test_alarm_low_and_validation(void)
{
    alarm_t     a;
    alarm_cfg_t c = {.set = 3000, .clear = 3100, .set_count = 1, .clear_count = 1, .dir = ALARM_LOW};
    CHECK(alarm_init(&a, &c));
    CHECK_EQ(alarm_update(&a, 3050), ALARM_EV_NONE);
    CHECK_EQ(alarm_update(&a, 2950), ALARM_EV_RAISED);
    CHECK_EQ(alarm_update(&a, 2800), ALARM_EV_NONE);
    CHECK_EQ(a.peak, 2800);
    CHECK_EQ(alarm_update(&a, 3100), ALARM_EV_CLEARED);

    /* No hysteresis gap -> rejected, previous config kept */
    alarm_cfg_t bad = {.set = 100, .clear = 100, .dir = ALARM_HIGH};
    CHECK(!alarm_cfg_valid(&bad));
    CHECK(!alarm_reconfigure(&a, &bad));
    CHECK_EQ(a.cfg.set, 3000);
    bad.dir = ALARM_LOW;
    CHECK(!alarm_cfg_valid(&bad));
    CHECK(!alarm_cfg_valid(NULL));

    /* Counts of 0 behave as 1 */
    alarm_cfg_t z = {.set = 10, .clear = 5, .set_count = 0, .clear_count = 0, .dir = ALARM_HIGH};
    CHECK(alarm_init(&a, &z));
    CHECK_EQ(alarm_update(&a, 10), ALARM_EV_RAISED);
    CHECK_EQ(alarm_update(&a, 5), ALARM_EV_CLEARED);
}

void test_adc_conversions(void)
{
    /* VREFINT_CAL typical ~1500 at 3.3 V. Reading the same code means VDDA = 3.3 V. */
    CHECK_EQ(adc_vdda_mv(1500, 1500), 3300);
    /* Lower supply -> VREFINT reads higher -> VDDA computed lower */
    CHECK_EQ(adc_vdda_mv(1650, 1500), 3000);
    CHECK_EQ(adc_vdda_mv(0, 1500), 0);
    CHECK_EQ(adc_vdda_mv(1502, 0), 3299); /* typical 1.21 V reference */

    CHECK_EQ(adc_raw_to_mv(0, 3300), 0);
    CHECK_EQ(adc_raw_to_mv(4095, 3300), 3300);
    CHECK_EQ(adc_raw_to_mv(2048, 3300), 1650);

    CHECK_EQ(adc_temp_cdeg(760), 2500);  /* V25 */
    CHECK_EQ(adc_temp_cdeg(810), 4500);  /* +50 mV / 2.5 mV = +20 degC */
    CHECK_EQ(adc_temp_cdeg(710), 500);
    CHECK_EQ(adc_temp_cdeg(690), -300);  /* -3.00 degC */
    CHECK_EQ(adc_temp_cdeg(761), 2540);
}

/**
 * @file    sigstats.h
 * @brief   Integer-only signal statistics for 12-bit ADC streams.
 *
 *  - ema_*  : exponential moving average in Q16 fixed point (no FPU in the ISR
 *             or hot path, deterministic rounding, no drift at steady state).
 *  - win_*  : block statistics over N samples: mean, min, max and the RMS of
 *             the AC component, sqrt(E[x^2] - E[x]^2). The AC RMS of a
 *             vibration / current-sensor channel is the classic condition-
 *             monitoring feature: it ignores DC offset and grows with energy.
 *  - isqrt  : exact floor square roots for 32- and 64-bit inputs.
 */
#ifndef LIB_SIGSTATS_H
#define LIB_SIGSTATS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- integer square root ------------------------------------------------ */
uint32_t isqrt32(uint32_t x);
uint32_t isqrt64(uint64_t x);

/* ---- exponential moving average ---------------------------------------- */
typedef struct {
    int32_t acc_q16; /* filtered value, Q16.16 */
    uint8_t shift;   /* alpha = 1 / 2^shift (1..15) */
    bool    primed;  /* first sample initialises the filter */
} ema_t;

void    ema_init(ema_t *f, uint8_t shift);
int32_t ema_update(ema_t *f, int32_t x); /* returns the rounded filtered value */
int32_t ema_value(const ema_t *f);

/* ---- windowed block statistics ------------------------------------------ */
typedef struct {
    uint16_t mean;
    uint16_t min;
    uint16_t max;
    uint16_t ac_rms; /* RMS of (x - mean), same units as x */
    uint16_t n;
} win_result_t;

typedef struct {
    uint64_t sum_sq;
    uint32_t sum;
    uint16_t n;
    uint16_t size; /* samples per window, 1..4096 */
    uint16_t min;
    uint16_t max;
} win_stats_t;

void win_init(win_stats_t *w, uint16_t size);
/** Add one sample; returns true and fills @p out when a window completes. */
bool win_add(win_stats_t *w, uint16_t x, win_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* LIB_SIGSTATS_H */

#include "sigstats.h"

#include <stddef.h>

/* Digit-by-digit (base 4) square root: exact floor(sqrt(x)), no division. */
uint32_t isqrt64(uint64_t x)
{
    uint64_t res = 0u;
    uint64_t bit = (uint64_t)1u << 62;

    while (bit > x) {
        bit >>= 2;
    }
    while (bit != 0u) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)res;
}

uint32_t isqrt32(uint32_t x)
{
    return isqrt64(x);
}

/* ---- EMA ----------------------------------------------------------------- */

void ema_init(ema_t *f, uint8_t shift)
{
    if (shift < 1u) {
        shift = 1u;
    } else if (shift > 15u) {
        shift = 15u;
    }
    f->acc_q16 = 0;
    f->shift   = shift;
    f->primed  = false;
}

int32_t ema_value(const ema_t *f)
{
    /* Round half away from zero when leaving Q16. */
    const int32_t half = 1 << 15;
    return (f->acc_q16 >= 0) ? (f->acc_q16 + half) / 65536 : -((-f->acc_q16 + half) / 65536);
}

int32_t ema_update(ema_t *f, int32_t x)
{
    /* Inputs are bounded to +/-32767 so that x * 65536 fits in int32_t. */
    if (x > 32767) {
        x = 32767;
    } else if (x < -32767) {
        x = -32767;
    }
    const int32_t x_q16 = x * 65536;

    if (!f->primed) {
        f->acc_q16 = x_q16;
        f->primed  = true;
    } else {
        /* Division (not >>) keeps the arithmetic well defined for negatives. */
        f->acc_q16 += (x_q16 - f->acc_q16) / (int32_t)(1 << f->shift);
    }
    return ema_value(f);
}

/* ---- windowed statistics ------------------------------------------------- */

static void win_reset(win_stats_t *w)
{
    w->sum_sq = 0u;
    w->sum    = 0u;
    w->n      = 0u;
    w->min    = UINT16_MAX;
    w->max    = 0u;
}

void win_init(win_stats_t *w, uint16_t size)
{
    if (size == 0u) {
        size = 1u;
    } else if (size > 4096u) {
        size = 4096u; /* keeps sum in 32 bits for 16-bit samples: 4096 * 65535 < 2^32 */
    }
    w->size = size;
    win_reset(w);
}

bool win_add(win_stats_t *w, uint16_t x, win_result_t *out)
{
    w->sum += x;
    w->sum_sq += (uint64_t)x * x;
    if (x < w->min) {
        w->min = x;
    }
    if (x > w->max) {
        w->max = x;
    }
    w->n++;

    if (w->n < w->size) {
        return false;
    }

    const uint64_t n = w->n;
    /* n^2 * variance = n * sum(x^2) - (sum x)^2, always >= 0 in exact arithmetic. */
    const uint64_t n_sum_sq = n * w->sum_sq;
    const uint64_t sum2     = (uint64_t)w->sum * w->sum;
    const uint64_t var_n2   = (n_sum_sq > sum2) ? (n_sum_sq - sum2) : 0u;

    if (out != NULL) {
        out->n      = w->n;
        out->mean   = (uint16_t)((w->sum + (w->n / 2u)) / w->n);
        out->min    = w->min;
        out->max    = w->max;
        out->ac_rms = (uint16_t)(isqrt64(var_n2) / w->n);
    }
    win_reset(w);
    return true;
}

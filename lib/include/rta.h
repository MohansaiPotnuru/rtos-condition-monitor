/**
 * @file    rta.h
 * @brief   Fixed-priority schedulability analysis for the task set.
 *
 * Two tests, from cheap to exact:
 *
 *  1. Liu & Layland utilisation bound: U = sum(C/T) <= n(2^(1/n) - 1).
 *     Sufficient, not necessary: a set may fail it and still be schedulable.
 *
 *  2. Response-time analysis (Joseph & Pandya 1986, Audsley et al. 1993):
 *        R_i = B_i + C_i + sum_{j in hp(i)} ceil(R_i / T_j) * C_j
 *     iterated to a fixed point. Exact for independent periodic tasks with
 *     deadlines <= periods under preemptive fixed priorities. B_i is the
 *     worst-case blocking from lower-priority tasks holding a shared mutex
 *     (bounded by priority inheritance).
 *
 * Priorities follow FreeRTOS: a larger number means higher priority. Tasks
 * of equal priority are time-sliced, so they are treated as interfering with
 * each other (the conservative choice).
 */
#ifndef LIB_RTA_H
#define LIB_RTA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint32_t    period_us;   /* T: period or minimum inter-arrival time */
    uint32_t    wcet_us;     /* C: worst-case execution time */
    uint32_t    deadline_us; /* D: relative deadline, 0 means D = T */
    uint32_t    blocking_us; /* B: worst-case blocking by lower priorities */
    uint8_t     priority;
} rta_task_t;

typedef struct {
    uint32_t response_us; /* R_i, or UINT32_MAX if it diverged past the deadline */
    bool     schedulable;
} rta_result_t;

/** Total utilisation in parts per million (1 000 000 == 100 %). */
uint32_t rta_utilization_ppm(const rta_task_t *tasks, size_t n);

/** Liu & Layland bound n(2^(1/n)-1) in ppm (ln 2 for n > 10). */
uint32_t rta_ll_bound_ppm(size_t n);

/**
 * Run response-time analysis for every task.
 * @return true if every task meets its deadline. @p results may be NULL.
 */
bool rta_analyse(const rta_task_t *tasks, size_t n, rta_result_t *results);

#ifdef __cplusplus
}
#endif

#endif /* LIB_RTA_H */

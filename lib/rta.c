#include "rta.h"

uint32_t rta_utilization_ppm(const rta_task_t *tasks, size_t n)
{
    uint64_t ppm = 0u;
    for (size_t i = 0u; i < n; i++) {
        if (tasks[i].period_us != 0u) {
            ppm += ((uint64_t)tasks[i].wcet_us * 1000000u) / tasks[i].period_us;
        }
    }
    return (ppm > UINT32_MAX) ? UINT32_MAX : (uint32_t)ppm;
}

uint32_t rta_ll_bound_ppm(size_t n)
{
    /* n(2^(1/n) - 1), precomputed so the firmware needs no libm. */
    static const uint32_t bound[] = {
        1000000u, 1000000u, 828427u, 779763u, 756828u, 743492u,
        734772u,  728627u,  724062u, 720538u, 717735u,
    };
    if (n == 0u) {
        return 1000000u;
    }
    if (n < sizeof(bound) / sizeof(bound[0])) {
        return bound[n];
    }
    return 693147u; /* lim n->inf = ln 2 */
}

static uint64_t ceil_div(uint64_t a, uint64_t b)
{
    return (a + b - 1u) / b;
}

bool rta_analyse(const rta_task_t *tasks, size_t n, rta_result_t *results)
{
    bool all_ok = true;

    for (size_t i = 0u; i < n; i++) {
        const rta_task_t *ti = &tasks[i];
        const uint64_t    d  = (ti->deadline_us != 0u) ? ti->deadline_us : ti->period_us;

        uint64_t r    = (uint64_t)ti->blocking_us + ti->wcet_us;
        bool     done = false;
        bool     ok   = false;

        while (!done) {
            uint64_t next = (uint64_t)ti->blocking_us + ti->wcet_us;
            for (size_t j = 0u; j < n; j++) {
                if (j == i || tasks[j].priority < ti->priority || tasks[j].period_us == 0u) {
                    continue;
                }
                next += ceil_div(r, tasks[j].period_us) * tasks[j].wcet_us;
            }
            if (next > d) {
                done = true; /* diverged past the deadline: unschedulable */
                r    = UINT64_MAX;
            } else if (next == r) {
                done = true; /* fixed point reached */
                ok   = true;
            } else {
                r = next;    /* monotonically non-decreasing, so this terminates */
            }
        }

        if (results != NULL) {
            results[i].schedulable = ok;
            results[i].response_us = ok ? (uint32_t)r : UINT32_MAX;
        }
        all_ok = all_ok && ok;
    }
    return all_ok;
}

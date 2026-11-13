#include "health.h"

#include <string.h>

void health_init(health_t *h, uint32_t expected_mask, uint8_t max_misses)
{
    memset(h, 0, sizeof(*h));
    h->expected   = expected_mask & ((1u << HEALTH_MAX_TASKS) - 1u);
    h->max_misses = max_misses;
    h->culprit    = 0xFFu;
}

bool health_evaluate(health_t *h, uint32_t seen)
{
    h->windows++;
    h->last_missing = h->expected & ~seen;

    for (uint8_t i = 0u; i < HEALTH_MAX_TASKS; i++) {
        const uint32_t bit = 1u << i;
        if ((h->expected & bit) == 0u) {
            continue;
        }
        if ((seen & bit) != 0u) {
            h->consecutive[i] = 0u;
            continue;
        }
        h->total_misses[i]++;
        if (h->consecutive[i] < UINT8_MAX) {
            h->consecutive[i]++;
        }
        if (h->consecutive[i] > h->max_misses && !h->tripped) {
            h->tripped = true;
            h->culprit = i;
        }
    }
    /* Once tripped, stay tripped: a recovered task does not hide a stall. */
    return !h->tripped;
}

#include "alarm.h"

#include <stddef.h>

bool alarm_cfg_valid(const alarm_cfg_t *cfg)
{
    if (cfg == NULL) {
        return false;
    }
    if (cfg->dir == ALARM_HIGH) {
        return cfg->clear < cfg->set;
    }
    if (cfg->dir == ALARM_LOW) {
        return cfg->clear > cfg->set;
    }
    return false;
}

bool alarm_init(alarm_t *a, const alarm_cfg_t *cfg)
{
    if (a == NULL || !alarm_cfg_valid(cfg)) {
        return false;
    }
    a->cfg     = *cfg;
    a->active  = false;
    a->latched = false;
    a->count   = 0u;
    a->trips   = 0u;
    a->peak    = 0;
    return true;
}

bool alarm_reconfigure(alarm_t *a, const alarm_cfg_t *cfg)
{
    if (a == NULL || !alarm_cfg_valid(cfg)) {
        return false;
    }
    a->cfg   = *cfg;
    a->count = 0u; /* state and statistics are kept */
    return true;
}

static bool beyond(alarm_dir_t dir, int32_t value, int32_t limit)
{
    return (dir == ALARM_HIGH) ? (value >= limit) : (value <= limit);
}

static bool within(alarm_dir_t dir, int32_t value, int32_t limit)
{
    return (dir == ALARM_HIGH) ? (value <= limit) : (value >= limit);
}

alarm_event_t alarm_update(alarm_t *a, int32_t value)
{
    const alarm_cfg_t *c       = &a->cfg;
    const uint8_t      need_on = (c->set_count == 0u) ? 1u : c->set_count;
    const uint8_t      need_off = (c->clear_count == 0u) ? 1u : c->clear_count;

    if (!a->active) {
        a->count = beyond(c->dir, value, c->set) ? (uint8_t)(a->count + 1u) : 0u;
        if (a->count >= need_on) {
            a->active  = true;
            a->latched = true;
            a->count   = 0u;
            a->trips++;
            a->peak = value;
            return ALARM_EV_RAISED;
        }
        return ALARM_EV_NONE;
    }

    if ((c->dir == ALARM_HIGH && value > a->peak) || (c->dir == ALARM_LOW && value < a->peak)) {
        a->peak = value;
    }
    a->count = within(c->dir, value, c->clear) ? (uint8_t)(a->count + 1u) : 0u;
    if (a->count >= need_off) {
        a->active = false;
        a->count  = 0u;
        return ALARM_EV_CLEARED;
    }
    return ALARM_EV_NONE;
}

bool alarm_ack(alarm_t *a)
{
    if (a->active) {
        return false;
    }
    a->latched = false;
    return true;
}

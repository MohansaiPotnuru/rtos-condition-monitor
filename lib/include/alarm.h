/**
 * @file    alarm.h
 * @brief   Threshold alarm with hysteresis, debounce counts and latching.
 *
 * A HIGH alarm raises after `set_count` consecutive samples >= `set` and
 * clears after `clear_count` consecutive samples <= `clear` (clear < set).
 * A LOW alarm is the mirror image. The gap between `set` and `clear` stops
 * chattering around the threshold; the counts reject single-sample spikes.
 *
 * Every raise also sets `latched`, which stays set until the operator
 * acknowledges it (button or CLI), so short excursions are never missed.
 */
#ifndef LIB_ALARM_H
#define LIB_ALARM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { ALARM_HIGH = 0, ALARM_LOW = 1 } alarm_dir_t;

typedef enum { ALARM_EV_NONE = 0, ALARM_EV_RAISED, ALARM_EV_CLEARED } alarm_event_t;

typedef struct {
    int32_t     set;
    int32_t     clear;
    uint8_t     set_count;   /* consecutive samples needed to raise (min 1) */
    uint8_t     clear_count; /* consecutive samples needed to clear (min 1) */
    alarm_dir_t dir;
} alarm_cfg_t;

typedef struct {
    alarm_cfg_t cfg;
    bool        active;
    bool        latched;
    uint8_t     count;
    uint32_t    trips;
    int32_t     peak; /* most extreme value seen while active */
} alarm_t;

/** @return false if the configuration is inconsistent (no hysteresis gap). */
bool          alarm_cfg_valid(const alarm_cfg_t *cfg);
bool          alarm_init(alarm_t *a, const alarm_cfg_t *cfg);
bool          alarm_reconfigure(alarm_t *a, const alarm_cfg_t *cfg);
alarm_event_t alarm_update(alarm_t *a, int32_t value);
/** Clears the latch; returns false (and keeps it) while the alarm is still active. */
bool          alarm_ack(alarm_t *a);

#ifdef __cplusplus
}
#endif

#endif /* LIB_ALARM_H */

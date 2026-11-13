/**
 * @file    app.h
 * @brief   Task set, shared state and inter-task interfaces.
 *
 *  task        prio  period     role
 *  ----------  ----  ---------  -------------------------------------------------
 *  tmr_svc       6   (100 ms)   FreeRTOS timer daemon: LED pattern, deferred ISR work
 *  supervisor    5   500 ms     task liveness -> IWDG, CPU load, stack watermarks
 *  sensor        4   10 ms      ADC sampling with vTaskDelayUntil (jitter-free)
 *  process       3   event      filters, window RMS, alarms (fed by the sensor queue)
 *  telemetry     2   100 ms*    CSV or COBS/CRC binary frames (*runtime adjustable)
 *  cli           1   sporadic   command shell on the ST-LINK VCP
 *
 * Priorities are rate-monotonic (shorter period = higher priority), except the
 * supervisor, which sits above the workers so that it can always observe them.
 */
#ifndef APP_APP_H
#define APP_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "alarm.h"
#include "event_groups.h"
#include "health.h"
#include "rta.h"
#include "telemetry.h"

#define APP_NAME    "rtos-condition-monitor"
#define APP_VERSION "1.0.0"

typedef enum {
    TASK_SENSOR = 0,
    TASK_PROCESS,
    TASK_TELEMETRY,
    TASK_CLI,
    TASK_SUPERVISOR,
    TASK_COUNT
} task_id_t;

typedef struct {
    const char *name;
    uint8_t     priority;
    uint16_t    stack_words;
    uint32_t    period_us;   /* period, or minimum inter-arrival time for sporadic tasks */
    uint32_t    budget_us;   /* design WCET budget used for analysis at boot */
    uint32_t    blocking_us; /* worst-case blocking on shared mutexes */
    bool        monitored;   /* must check in with the supervisor */
} task_spec_t;

extern const task_spec_t g_task_spec[TASK_COUNT];

/* Execution-time measurement (TIM2, 1 us). Written only by the owning task. */
typedef struct {
    uint32_t last_us;
    uint32_t max_us;
    uint32_t runs;
    uint32_t deadline_misses; /* vTaskDelayUntil found the next release already passed */
} exec_stats_t;

extern exec_stats_t g_exec[TASK_COUNT];

/* One ADC sample set, sensor -> process. */
typedef struct {
    uint32_t t_us;
    uint16_t ain;
    uint16_t vref;
    uint16_t temp;
    uint8_t  errors; /* bit per failed conversion */
} sample_t;

typedef enum { TLM_MODE_OFF = 0, TLM_MODE_CSV, TLM_MODE_BIN } tlm_mode_t;

typedef enum { ALARM_ID_TEMP = 0, ALARM_ID_VDDA, ALARM_ID_RMS, ALARM_ID_COUNT } alarm_id_t;

typedef struct {
    uint32_t samples;
    uint32_t windows;
    uint32_t adc_errors;
    uint32_t queue_overflows;
    uint32_t tlm_frames;
    uint16_t queue_high_water;
} app_counters_t;

/* ---- shared services (thread-safe) -------------------------------------- */
void       app_checkin(task_id_t id);
void       app_snapshot_get(tlm_snapshot_t *out);
void       app_alarms_get(alarm_t out[ALARM_ID_COUNT]);
bool       app_alarm_configure(alarm_id_t id, int32_t set, int32_t clear);
void       app_alarms_ack(void);
void       app_counters_get(app_counters_t *out);
uint8_t    app_cpu_load_pct(void);
const health_t *app_health(void);

tlm_mode_t app_tlm_mode(void);
uint32_t   app_tlm_period_ms(void);
void       app_tlm_set(tlm_mode_t mode, uint32_t period_ms);

/* Fault injection for recovery testing (CLI `crash`). */
void app_inject_stall(task_id_t id);

/* Build the RTA task table from budgets and (optionally) measured WCETs. */
size_t app_rta_table(rta_task_t *out, size_t cap, bool use_measured);

/* ---- entry points -------------------------------------------------------- */
void app_tasks_create(void);
void app_cli_task(void *arg);
void app_print(const char *s);
void app_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* APP_APP_H */

/**
 * @file    app_tasks.c
 * @brief   Task bodies, shared state and kernel static-memory callbacks.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "adc_conv.h"
#include "app.h"
#include "bsp.h"
#include "fault.h"
#include "queue.h"
#include "semphr.h"
#include "sigstats.h"
#include "task.h"
#include "timers.h"

/* ---------------------------------------------------------- task table ---- */

#define SENSOR_PERIOD_MS     10u
#define SUPERVISOR_PERIOD_MS 500u
#define WINDOW_SAMPLES       100u /* 1 s RMS window at 100 Hz */
#define DECIMATE             10u  /* temperature/VDDA alarms at 10 Hz */
#define WDT_TIMEOUT_MS       2000u
#define WDT_MAX_MISSES       2u   /* tolerate 2 late windows (1 s) before starving the IWDG */
#define SAMPLE_QUEUE_LEN     16u

/* Stack sizes in 32-bit words; one definition feeds both the table and the arrays. */
#define STACK_SENSOR     256u
#define STACK_PROCESS    512u
#define STACK_TELEMETRY  512u
#define STACK_CLI        768u
#define STACK_SUPERVISOR 512u

const task_spec_t g_task_spec[TASK_COUNT] = {
    /*                name          prio stack             period_us budget_us blocking_us monitored */
    [TASK_SENSOR]     = {"sensor",     4, STACK_SENSOR,      10000,     150,      0,   true},
    [TASK_PROCESS]    = {"process",    3, STACK_PROCESS,     10000,     150,     50,   true},
    [TASK_TELEMETRY]  = {"telemetry",  2, STACK_TELEMETRY,  100000,    1000,  10000,   true},
    [TASK_CLI]        = {"cli",        1, STACK_CLI,         50000,    5000,      0,   true},
    [TASK_SUPERVISOR] = {"supervisor", 5, STACK_SUPERVISOR, 500000,     300,      0,  false},
};

exec_stats_t g_exec[TASK_COUNT];

static StaticTask_t s_tcb[TASK_COUNT];
static StackType_t  s_stack_sensor[STACK_SENSOR], s_stack_process[STACK_PROCESS], s_stack_tlm[STACK_TELEMETRY],
    s_stack_cli[STACK_CLI], s_stack_sup[STACK_SUPERVISOR];
_Static_assert(TASK_COUNT <= HEALTH_MAX_TASKS, "health monitor tracks at most 8 tasks");
_Static_assert(configTIMER_TASK_PRIORITY < configMAX_PRIORITIES, "timer daemon priority out of range");
static StackType_t *const s_stacks[TASK_COUNT] = {s_stack_sensor, s_stack_process, s_stack_tlm, s_stack_cli,
                                                  s_stack_sup};
static TaskHandle_t s_task[TASK_COUNT];

/* ---------------------------------------------------------- shared state -- */

static StaticQueue_t      s_sample_q_buf;
static uint8_t            s_sample_q_store[SAMPLE_QUEUE_LEN * sizeof(sample_t)];
static QueueHandle_t      s_sample_q;
static StaticSemaphore_t  s_state_mutex_buf;
static SemaphoreHandle_t  s_state_mutex; /* guards s_snap, s_alarm, s_counters */
static StaticEventGroup_t s_health_eg_buf, s_events_eg_buf;
static EventGroupHandle_t s_health_eg, s_events_eg;
static StaticTimer_t      s_led_timer_buf;

#define EVT_ACK (1u << 0)

static tlm_snapshot_t s_snap;
static alarm_t        s_alarm[ALARM_ID_COUNT];
static app_counters_t s_counters;
static health_t       s_health;
static volatile uint8_t    s_cpu_pct;
static volatile tlm_mode_t s_tlm_mode      = TLM_MODE_CSV;
static volatile uint32_t   s_tlm_period_ms = 1000u;
static volatile uint32_t   s_stall_mask;      /* fault injection */
static volatile uint32_t   s_queue_overflows; /* written only by the sensor task, lock-free */

static void lock(void)
{
    (void)xSemaphoreTake(s_state_mutex, portMAX_DELAY);
}

static void unlock(void)
{
    (void)xSemaphoreGive(s_state_mutex);
}

void app_checkin(task_id_t id)
{
    if ((s_stall_mask & (1u << id)) == 0u) {
        (void)xEventGroupSetBits(s_health_eg, (EventBits_t)(1u << id));
    }
}

void app_inject_stall(task_id_t id)
{
    s_stall_mask |= (1u << id);
}

void app_snapshot_get(tlm_snapshot_t *out)
{
    lock();
    *out = s_snap;
    unlock();
    out->cpu_pct = s_cpu_pct;
}

void app_alarms_get(alarm_t out[ALARM_ID_COUNT])
{
    lock();
    memcpy(out, s_alarm, sizeof s_alarm);
    unlock();
}

bool app_alarm_configure(alarm_id_t id, int32_t set, int32_t clear)
{
    if ((unsigned)id >= ALARM_ID_COUNT) {
        return false;
    }
    lock();
    alarm_cfg_t c = s_alarm[id].cfg;
    c.set         = set;
    c.clear       = clear;
    const bool ok = alarm_reconfigure(&s_alarm[id], &c);
    unlock();
    return ok;
}

void app_alarms_ack(void)
{
    (void)xEventGroupSetBits(s_events_eg, EVT_ACK);
}

void app_counters_get(app_counters_t *out)
{
    lock();
    *out = s_counters;
    unlock();
    out->queue_overflows = s_queue_overflows; /* aligned 32-bit read: atomic on Cortex-M */
}

uint8_t app_cpu_load_pct(void)
{
    return s_cpu_pct;
}

const health_t *app_health(void)
{
    return &s_health;
}

tlm_mode_t app_tlm_mode(void)
{
    return s_tlm_mode;
}

uint32_t app_tlm_period_ms(void)
{
    return s_tlm_period_ms;
}

void app_tlm_set(tlm_mode_t mode, uint32_t period_ms)
{
    if (period_ms != 0u) {
        s_tlm_period_ms = period_ms;
    }
    s_tlm_mode = mode;
}

size_t app_rta_table(rta_task_t *out, size_t cap, bool use_measured)
{
    size_t n = 0u;
    for (size_t i = 0u; i < TASK_COUNT && n < cap; i++, n++) {
        const task_spec_t *s = &g_task_spec[i];
        uint32_t           c = s->budget_us;
        if (use_measured && g_exec[i].runs > 0u && g_exec[i].max_us > c) {
            c = g_exec[i].max_us; /* measured worst case exceeded the budget */
        }
        out[n] = (rta_task_t){s->name, s->period_us, c, 0u, s->blocking_us, s->priority};
    }
    if (n < cap) {
        /* Timer daemon: LED pattern every 100 ms, a few microseconds of work. */
        out[n++] = (rta_task_t){"tmr_svc", 100000u, 20u, 0u, 0u, configTIMER_TASK_PRIORITY};
    }
    return n;
}

void app_print(const char *s)
{
    bsp_uart_puts(s);
}

void app_printf(const char *fmt, ...)
{
    char    buf[160];
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        (void)bsp_uart_write(buf, ((size_t)n < sizeof buf) ? (size_t)n : sizeof buf - 1u, 200u);
    }
}

static void exec_record(task_id_t id, uint32_t start_us)
{
    const uint32_t dt = bsp_time_us() - start_us;
    exec_stats_t  *e  = &g_exec[id];
    e->last_us        = dt;
    if (dt > e->max_us) {
        e->max_us = dt;
    }
    e->runs++;
}

/* ----------------------------------------------------------- sensor task -- */

static void sensor_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();

    for (;;) {
        if (xTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS)) == pdFALSE) {
            g_exec[TASK_SENSOR].deadline_misses++; /* release time had already passed */
        }
        const uint32_t t0 = bsp_time_us();
        sample_t       s  = {.t_us = t0};

        if (!bsp_adc_read(BSP_ADC_CH_AIN, &s.ain)) {
            s.errors |= 0x01u;
        }
        if (!bsp_adc_read(BSP_ADC_CH_VREFINT, &s.vref)) {
            s.errors |= 0x02u;
        }
        if (!bsp_adc_read(BSP_ADC_CH_TEMP, &s.temp)) {
            s.errors |= 0x04u;
        }
        if (xQueueSend(s_sample_q, &s, 0) != pdPASS) {
            s_queue_overflows++; /* never block the sampler, not even on a mutex */
        }
        exec_record(TASK_SENSOR, t0);
        app_checkin(TASK_SENSOR);
    }
}

/* ---------------------------------------------------------- process task -- */

static const alarm_cfg_t k_alarm_defaults[ALARM_ID_COUNT] = {
    [ALARM_ID_TEMP] = {.set = 6000, .clear = 5500, .set_count = 5, .clear_count = 5, .dir = ALARM_HIGH},
    [ALARM_ID_VDDA] = {.set = 3000, .clear = 3100, .set_count = 5, .clear_count = 5, .dir = ALARM_LOW},
    [ALARM_ID_RMS]  = {.set = 300, .clear = 250, .set_count = 2, .clear_count = 2, .dir = ALARM_HIGH},
};

static const char *const k_alarm_name[ALARM_ID_COUNT] = {"TEMP", "VDDA", "RMS"};

static uint8_t alarm_bits(void)
{
    uint8_t bits = 0u;
    for (unsigned i = 0u; i < ALARM_ID_COUNT; i++) {
        if (s_alarm[i].active) {
            bits |= (uint8_t)(1u << i);
        }
        if (s_alarm[i].latched) {
            bits |= (uint8_t)(1u << (i + TLM_LATCH_SHIFT));
        }
    }
    return bits;
}

static void report_event(alarm_id_t id, alarm_event_t ev, int32_t value)
{
    if (ev == ALARM_EV_RAISED) {
        app_printf("alarm: %s RAISED (value %ld)\r\n", k_alarm_name[id], (long)value);
    } else if (ev == ALARM_EV_CLEARED) {
        app_printf("alarm: %s cleared\r\n", k_alarm_name[id]);
    }
}

static void process_task(void *arg)
{
    (void)arg;
    ema_t        temp_f, vdda_f;
    win_stats_t  win;
    win_result_t wr;
    uint32_t     n = 0u;
    const uint16_t cal = bsp_adc_vrefint_cal();

    ema_init(&temp_f, 4); /* ~16-sample time constant: 160 ms */
    ema_init(&vdda_f, 3);
    win_init(&win, WINDOW_SAMPLES);

    for (;;) {
        sample_t s;
        if (xQueueReceive(s_sample_q, &s, pdMS_TO_TICKS(100)) != pdPASS) {
            app_checkin(TASK_PROCESS); /* idle but alive; a dead sensor shows up on its own bit */
            continue;
        }
        const uint32_t t0 = bsp_time_us();

        const UBaseType_t backlog = uxQueueMessagesWaiting(s_sample_q) + 1u;

        if (xEventGroupClearBits(s_events_eg, EVT_ACK) & EVT_ACK) {
            lock();
            for (unsigned i = 0u; i < ALARM_ID_COUNT; i++) {
                (void)alarm_ack(&s_alarm[i]); /* still-active alarms stay latched */
            }
            s_snap.alarms = alarm_bits();
            unlock();
            app_print("alarms acknowledged\r\n");
        }

        if (s.errors != 0u) {
            lock();
            s_counters.adc_errors++;
            s_counters.samples++;
            unlock();
            exec_record(TASK_PROCESS, t0);
            app_checkin(TASK_PROCESS);
            continue;
        }

        const uint32_t vdda    = (uint32_t)ema_update(&vdda_f, (int32_t)adc_vdda_mv(s.vref, cal));
        const int32_t  temp_c  = ema_update(&temp_f, adc_temp_cdeg(adc_raw_to_mv(s.temp, vdda)));
        const bool     window  = win_add(&win, s.ain, &wr);
        n++;

        alarm_event_t ev[ALARM_ID_COUNT] = {ALARM_EV_NONE, ALARM_EV_NONE, ALARM_EV_NONE};
        uint16_t      rms_mv             = 0u;

        lock();
        s_counters.samples++;
        if (backlog > s_counters.queue_high_water) {
            s_counters.queue_high_water = (uint16_t)backlog;
        }
        s_snap.temp_cdeg = (int16_t)temp_c;
        s_snap.vdda_mv   = (uint16_t)vdda;
        if (n % DECIMATE == 0u) {
            ev[ALARM_ID_TEMP] = alarm_update(&s_alarm[ALARM_ID_TEMP], temp_c);
            ev[ALARM_ID_VDDA] = alarm_update(&s_alarm[ALARM_ID_VDDA], (int32_t)vdda);
        }
        if (window) {
            s_counters.windows++;
            s_snap.ain_mv     = (uint16_t)adc_raw_to_mv(wr.mean, vdda);
            s_snap.ain_min_mv = (uint16_t)adc_raw_to_mv(wr.min, vdda);
            s_snap.ain_max_mv = (uint16_t)adc_raw_to_mv(wr.max, vdda);
            rms_mv            = (uint16_t)adc_raw_to_mv(wr.ac_rms, vdda);
            s_snap.ain_rms_mv = rms_mv;
            ev[ALARM_ID_RMS]  = alarm_update(&s_alarm[ALARM_ID_RMS], rms_mv);
        }
        s_snap.alarms = alarm_bits();
        unlock();

        /* Print outside the lock: UART output may block. */
        report_event(ALARM_ID_TEMP, ev[ALARM_ID_TEMP], temp_c);
        report_event(ALARM_ID_VDDA, ev[ALARM_ID_VDDA], (int32_t)vdda);
        report_event(ALARM_ID_RMS, ev[ALARM_ID_RMS], rms_mv);

        exec_record(TASK_PROCESS, t0);
        app_checkin(TASK_PROCESS);
    }
}

/* -------------------------------------------------------- telemetry task -- */

static void telemetry_task(void *arg)
{
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    uint16_t   seq  = 0u;
    tlm_mode_t prev = TLM_MODE_OFF;

    for (;;) {
        /* Sleep in short slices so a long period never starves the check-in. */
        const TickType_t period = pdMS_TO_TICKS(app_tlm_period_ms());
        while ((xTaskGetTickCount() - last) < period) {
            const TickType_t left = period - (xTaskGetTickCount() - last);
            vTaskDelay((left < pdMS_TO_TICKS(200)) ? left : pdMS_TO_TICKS(200));
            app_checkin(TASK_TELEMETRY);
        }
        last += period;
        if ((xTaskGetTickCount() - last) >= period) {
            last = xTaskGetTickCount(); /* fell behind (e.g. period shortened): resynchronise */
            g_exec[TASK_TELEMETRY].deadline_misses++;
        }

        const uint32_t   t0   = bsp_time_us();
        const tlm_mode_t mode = app_tlm_mode();
        if (mode != TLM_MODE_OFF) {
            tlm_snapshot_t snap;
            app_snapshot_get(&snap);
            const uint32_t up = (uint32_t)xTaskGetTickCount();
            if (mode == TLM_MODE_CSV) {
                char line[96];
                if (prev != TLM_MODE_CSV) {
                    app_print(tlm_csv_header());
                }
                const int len = tlm_format_csv(&snap, seq, up, line, sizeof line);
                if (len > 0) {
                    (void)bsp_uart_write(line, (size_t)len, 50u);
                }
            } else {
                uint8_t      wire[TLM_WIRE_MAX];
                const size_t len = tlm_encode_wire(&snap, seq, up, wire, sizeof wire);
                (void)bsp_uart_write(wire, len, 50u);
            }
            seq++;
            lock();
            s_counters.tlm_frames++;
            unlock();
        }
        prev = mode;
        exec_record(TASK_TELEMETRY, t0);
        app_checkin(TASK_TELEMETRY);
    }
}

/* ------------------------------------------------------- supervisor task -- */

static void supervisor_task(void *arg)
{
    (void)arg;
    TickType_t last      = xTaskGetTickCount();
    uint32_t   idle_prev = ulTaskGetIdleRunTimeCounter();
    uint32_t   time_prev = bsp_time_us();
    bool       reported  = false;
    uint32_t   expected  = 0u;

    for (unsigned i = 0u; i < TASK_COUNT; i++) {
        if (g_task_spec[i].monitored) {
            expected |= 1u << i;
        }
    }
    health_init(&s_health, expected, WDT_MAX_MISSES);
    bsp_iwdg_start(WDT_TIMEOUT_MS);

    for (;;) {
        (void)xTaskDelayUntil(&last, pdMS_TO_TICKS(SUPERVISOR_PERIOD_MS));
        const uint32_t t0 = bsp_time_us();

        /* Check-ins collected during the window (read and cleared atomically). */
        const EventBits_t seen = xEventGroupClearBits(s_health_eg, (EventBits_t)expected);
        if (health_evaluate(&s_health, (uint32_t)seen)) {
            bsp_iwdg_kick();
        } else if (!reported) {
            reported            = true;
            const unsigned who  = s_health.culprit;
            const char    *name = (who < TASK_COUNT) ? g_task_spec[who].name : "?";
            app_printf("supervisor: task '%s' stalled, watchdog will reset the MCU\r\n", name);
            fault_note(FAULT_WATCHDOG, name, s_health.windows);
#if defined(TARGET_QEMU)
            fault_fatal(FAULT_WATCHDOG, name, s_health.windows); /* QEMU has no IWDG */
#endif
        }

        /* CPU load over the window = 1 - idle time / wall time. */
        const uint32_t idle_now = ulTaskGetIdleRunTimeCounter();
        const uint32_t time_now = bsp_time_us();
        const uint32_t d_idle   = idle_now - idle_prev;
        const uint32_t d_time   = time_now - time_prev;
        if (d_time > 0u && d_idle <= d_time) {
            s_cpu_pct = (uint8_t)(100u - (uint32_t)(((uint64_t)d_idle * 100u) / d_time));
        }
        idle_prev = idle_now;
        time_prev = time_now;

        exec_record(TASK_SUPERVISOR, t0);
    }
}

/* ------------------------------------------------------- LED and button --- */

static void led_timer_cb(TimerHandle_t t)
{
    (void)t;
    static uint8_t phase;
    phase           = (uint8_t)((phase + 1u) % 10u); /* 10 x 100 ms = 1 s pattern */
    const uint8_t a = s_snap.alarms;                 /* single byte read: atomic */
    bool          on;
    if (a & 0x0Fu) {
        on = (phase & 1u) != 0u;                     /* active alarm: 5 Hz blink */
    } else if (a & 0xF0u) {
        on = (phase == 0u || phase == 2u);           /* latched: double blink */
    } else {
        on = (phase == 0u);                          /* healthy: 100 ms heartbeat */
    }
    bsp_led_set(on);
}

static void on_button_isr(void)
{
    BaseType_t woken = pdFALSE;
    (void)xEventGroupSetBitsFromISR(s_events_eg, EVT_ACK, &woken);
    portYIELD_FROM_ISR(woken);
}

/* --------------------------------------------------------------- create --- */

static const TaskFunction_t k_entry[TASK_COUNT] = {
    [TASK_SENSOR] = sensor_task,       [TASK_PROCESS] = process_task, [TASK_TELEMETRY] = telemetry_task,
    [TASK_CLI] = app_cli_task,         [TASK_SUPERVISOR] = supervisor_task,
};

void app_tasks_create(void)
{
    s_sample_q    = xQueueCreateStatic(SAMPLE_QUEUE_LEN, sizeof(sample_t), s_sample_q_store, &s_sample_q_buf);
    s_state_mutex = xSemaphoreCreateMutexStatic(&s_state_mutex_buf);
    s_health_eg   = xEventGroupCreateStatic(&s_health_eg_buf);
    s_events_eg   = xEventGroupCreateStatic(&s_events_eg_buf);
    configASSERT(s_sample_q && s_state_mutex && s_health_eg && s_events_eg);
    vQueueAddToRegistry(s_sample_q, "samples");
    vQueueAddToRegistry(s_state_mutex, "state");

    for (unsigned i = 0u; i < ALARM_ID_COUNT; i++) {
        const bool ok = alarm_init(&s_alarm[i], &k_alarm_defaults[i]);
        configASSERT(ok);
        (void)ok;
    }

    for (unsigned i = 0u; i < TASK_COUNT; i++) {
        const task_spec_t *s = &g_task_spec[i];
        s_task[i] = xTaskCreateStatic(k_entry[i], s->name, s->stack_words, NULL, s->priority, s_stacks[i], &s_tcb[i]);
        configASSERT(s_task[i] != NULL);
    }

    TimerHandle_t led = xTimerCreateStatic("led", pdMS_TO_TICKS(100), pdTRUE, NULL, led_timer_cb, &s_led_timer_buf);
    configASSERT(led != NULL);
    (void)xTimerStart(led, 0);

    bsp_button_init(on_button_isr);
}

/* ------------------------------------------- kernel static-memory hooks --- */

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *size)
{
    static StaticTask_t idle_tcb;
    static StackType_t  idle_stack[configMINIMAL_STACK_SIZE];
    *tcb   = &idle_tcb;
    *stack = idle_stack;
    *size  = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **tcb, StackType_t **stack, configSTACK_DEPTH_TYPE *size)
{
    static StaticTask_t timer_tcb;
    static StackType_t  timer_stack[configTIMER_TASK_STACK_DEPTH];
    *tcb   = &timer_tcb;
    *stack = timer_stack;
    *size  = configTIMER_TASK_STACK_DEPTH;
}

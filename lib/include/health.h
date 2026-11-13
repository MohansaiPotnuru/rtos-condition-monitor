/**
 * @file    health.h
 * @brief   Task-liveness policy behind the watchdog supervisor.
 *
 * Kicking a hardware watchdog from the idle loop only proves the CPU is
 * running. This policy proves every task is: each monitored task sets its
 * bit once per supervision window, and the supervisor feeds the watchdog
 * only while no task has missed more than `max_misses` windows in a row.
 * One late window (e.g. a burst of CLI output) is tolerated; a task that is
 * deadlocked or starved stops the kicks and the IWDG resets the MCU.
 */
#ifndef LIB_HEALTH_H
#define LIB_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HEALTH_MAX_TASKS 8u

typedef struct {
    uint32_t expected;                       /* bit i set => task i is monitored */
    uint8_t  max_misses;                     /* consecutive misses tolerated */
    uint8_t  consecutive[HEALTH_MAX_TASKS];  /* current run of missed windows */
    uint32_t total_misses[HEALTH_MAX_TASKS];
    uint32_t windows;
    uint32_t last_missing;                   /* mask of tasks missing in the last window */
    bool     tripped;                        /* sticky: watchdog feeding has stopped */
    uint8_t  culprit;                        /* first task that exceeded max_misses */
} health_t;

void health_init(health_t *h, uint32_t expected_mask, uint8_t max_misses);

/**
 * Evaluate one supervision window.
 * @param seen  check-in bits collected during the window.
 * @return true if the watchdog may be fed.
 */
bool health_evaluate(health_t *h, uint32_t seen);

#ifdef __cplusplus
}
#endif

#endif /* LIB_HEALTH_H */

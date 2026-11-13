/**
 * @file    fault.h
 * @brief   Post-mortem fault records that survive a reset.
 *
 * HardFault, failed configASSERT, stack overflow and watchdog-supervisor
 * trips all write a record into a .noinit RAM section, print it over the
 * polled UART, then reset. On the next boot the record is reported (and
 * kept until `fault clear`), so a field unit that rebooted itself can still
 * tell you why: faulting PC, LR, CFSR/HFSR and the fault address registers.
 */
#ifndef PLATFORM_FAULT_H
#define PLATFORM_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FAULT_NONE = 0,
    FAULT_HARDFAULT,
    FAULT_ASSERT,
    FAULT_STACK_OVERFLOW,
    FAULT_WATCHDOG,
} fault_type_t;

typedef struct {
    uint32_t magic;
    uint32_t type;
    uint32_t pc;
    uint32_t lr;
    uint32_t psr;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t line;
    uint32_t uptime_ms;
    char     info[24]; /* file, task name or watchdog culprit */
} fault_record_t;

/** Call first thing at boot: counts boots and validates the stored record. */
void fault_boot(void);

uint32_t fault_boot_count(void);
bool     fault_get(fault_record_t *out); /* false if no valid record */
void     fault_clear(void);
void     fault_print(const fault_record_t *r, void (*puts_fn)(const char *));
const char *fault_type_str(uint32_t type);

/** Record without resetting (e.g. just before deliberately starving the IWDG). */
void fault_note(fault_type_t type, const char *info, uint32_t line);

/** Record, print (polled) and reset. Never returns. */
__attribute__((noreturn)) void fault_fatal(fault_type_t type, const char *info, uint32_t line);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_FAULT_H */

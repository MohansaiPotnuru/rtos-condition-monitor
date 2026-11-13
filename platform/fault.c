#include "fault.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "bsp.h"
#include "crc16.h"
#include "stm32f4xx.h"
#include "task.h"

#define FAULT_MAGIC 0xFA017EC5u
#define BOOT_MAGIC  0xB0075AFEu

typedef struct {
    fault_record_t rec;
    uint16_t       crc;
} fault_slot_t;

typedef struct {
    uint32_t magic;
    uint32_t count;
} boot_slot_t;

static fault_slot_t s_fault __attribute__((section(".noinit")));
static boot_slot_t  s_boot __attribute__((section(".noinit")));
static bool         s_have_fault;

static uint16_t slot_crc(const fault_record_t *r)
{
    return crc16_ccitt(r, sizeof(*r), CRC16_INIT);
}

void fault_boot(void)
{
    if (s_boot.magic != BOOT_MAGIC) {
        s_boot.magic = BOOT_MAGIC; /* first power-up: RAM content is random */
        s_boot.count = 0u;
    }
    s_boot.count++;

    s_have_fault = (s_fault.rec.magic == FAULT_MAGIC) && (s_fault.crc == slot_crc(&s_fault.rec));
    if (!s_have_fault) {
        memset(&s_fault, 0, sizeof s_fault);
    }
}

uint32_t fault_boot_count(void)
{
    return s_boot.count;
}

bool fault_get(fault_record_t *out)
{
    if (!s_have_fault) {
        return false;
    }
    *out = s_fault.rec;
    return true;
}

void fault_clear(void)
{
    taskENTER_CRITICAL();
    memset(&s_fault, 0, sizeof s_fault);
    s_have_fault = false;
    taskEXIT_CRITICAL();
}

const char *fault_type_str(uint32_t type)
{
    switch (type) {
    case FAULT_HARDFAULT:      return "HARDFAULT";
    case FAULT_ASSERT:         return "ASSERT";
    case FAULT_STACK_OVERFLOW: return "STACK_OVERFLOW";
    case FAULT_WATCHDOG:       return "WATCHDOG";
    default:                   return "NONE";
    }
}

void fault_print(const fault_record_t *r, void (*puts_fn)(const char *))
{
    char line[96];
    snprintf(line, sizeof line, "fault: %s '%s' line %lu at %lu ms\r\n", fault_type_str(r->type), r->info,
             (unsigned long)r->line, (unsigned long)r->uptime_ms);
    puts_fn(line);
    if (r->type == FAULT_HARDFAULT) {
        snprintf(line, sizeof line, "  pc=0x%08lX lr=0x%08lX psr=0x%08lX\r\n", (unsigned long)r->pc,
                 (unsigned long)r->lr, (unsigned long)r->psr);
        puts_fn(line);
        snprintf(line, sizeof line, "  cfsr=0x%08lX hfsr=0x%08lX mmfar=0x%08lX bfar=0x%08lX\r\n",
                 (unsigned long)r->cfsr, (unsigned long)r->hfsr, (unsigned long)r->mmfar, (unsigned long)r->bfar);
        puts_fn(line);
    }
}

static uint32_t uptime_ms_safe(void)
{
    /* With a 32-bit tick, xTaskGetTickCount() is a single aligned load with no
     * critical section and no asserts, so it is safe even inside a fault handler. */
    return (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) ? (uint32_t)xTaskGetTickCount() : 0u;
}

static void store(fault_record_t *r, fault_type_t type, const char *info, uint32_t line)
{
    r->magic     = FAULT_MAGIC;
    r->type      = (uint32_t)type;
    r->line      = line;
    r->uptime_ms = uptime_ms_safe();
    r->cfsr      = SCB->CFSR;
    r->hfsr      = SCB->HFSR;
    r->mmfar     = SCB->MMFAR;
    r->bfar      = SCB->BFAR;
    memset(r->info, 0, sizeof r->info);
    if (info != NULL) {
        /* Keep the tail of long strings: for file paths it is the useful part. */
        const size_t n = strlen(info);
        const char  *p = (n >= sizeof r->info) ? info + (n - (sizeof r->info - 1u)) : info;
        memcpy(r->info, p, strlen(p));
    }
}

static __attribute__((noreturn)) void commit_and_reset(void)
{
    s_fault.crc  = slot_crc(&s_fault.rec);
    s_have_fault = true;
    bsp_uart_puts_polled("\r\n*** FATAL ***\r\n");
    fault_print(&s_fault.rec, bsp_uart_puts_polled);
    bsp_uart_puts_polled("resetting...\r\n");
#if defined(TARGET_QEMU)
    /* QEMU: stay put so CI captures the report instead of boot-looping. */
    for (;;) {
        __WFI();
    }
#else
    for (volatile uint32_t i = 0; i < 2000000u; i++) {
        /* let the last bytes leave the UART */
    }
    bsp_system_reset();
    for (;;) {
    }
#endif
}

void fault_note(fault_type_t type, const char *info, uint32_t line)
{
    taskENTER_CRITICAL();
    store(&s_fault.rec, type, info, line);
    s_fault.crc  = slot_crc(&s_fault.rec);
    s_have_fault = true;
    taskEXIT_CRITICAL();
}

void fault_fatal(fault_type_t type, const char *info, uint32_t line)
{
    __disable_irq();
    store(&s_fault.rec, type, info, line);
    commit_and_reset();
}

/* Called from the naked HardFault_Handler with the stacked exception frame:
 * r0, r1, r2, r3, r12, lr, pc, xpsr. */
void fault_hardfault_c(uint32_t *frame, uint32_t exc_return)
{
    (void)exc_return;
    __disable_irq();
    store(&s_fault.rec, FAULT_HARDFAULT, "hardfault", 0u);
    s_fault.rec.lr  = frame[5];
    s_fault.rec.pc  = frame[6];
    s_fault.rec.psr = frame[7];
    commit_and_reset();
}

/* ---- kernel hooks ------------------------------------------------------- */

void app_assert_failed(const char *file, int line)
{
    fault_fatal(FAULT_ASSERT, file, (uint32_t)line);
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    fault_fatal(FAULT_STACK_OVERFLOW, name, 0u);
}

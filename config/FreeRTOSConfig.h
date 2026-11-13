/**
 * @file    FreeRTOSConfig.h
 * @brief   Kernel configuration for STM32F401RE (Cortex-M4F, 4 NVIC priority bits).
 *
 * Design choices worth calling out:
 *  - Fully static allocation (configSUPPORT_DYNAMIC_ALLOCATION = 0): no heap,
 *    no fragmentation, and every byte of RAM is accounted for at link time.
 *  - 1 kHz tick, preemption on, time slicing on for equal priorities.
 *  - Run-time stats from TIM2 at 1 MHz (not the tick), so per-task CPU usage
 *    is measured at microsecond resolution.
 *  - Interrupt priorities: numerically 0..4 can never call the kernel and are
 *    never masked (reserved for hard real-time ISRs); 5..15 may use the
 *    FromISR API. SysTick and PendSV run at the lowest priority (15).
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__GNUC__) && !defined(__ASSEMBLER__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
void            app_assert_failed(const char *file, int line);
void            bsp_timebase_init(void);
uint32_t        bsp_time_us(void);
#endif

/* ---- scheduler -------------------------------------------------------- */
#define configUSE_PREEMPTION                     1
#define configUSE_TIME_SLICING                   1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configUSE_TICKLESS_IDLE                  0
#define configCPU_CLOCK_HZ                       (SystemCoreClock)
#define configTICK_RATE_HZ                       ((TickType_t)1000)
#define configMAX_PRIORITIES                     7
#define configMINIMAL_STACK_SIZE                 ((uint16_t)128)
#define configMAX_TASK_NAME_LEN                  12
#define configTICK_TYPE_WIDTH_IN_BITS            TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD                  1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    1
#define configENABLE_BACKWARD_COMPATIBILITY      0
#define configUSE_NEWLIB_REENTRANT               0

/* ---- memory ----------------------------------------------------------- */
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         0
#define configKERNEL_PROVIDED_STATIC_MEMORY      0 /* app provides idle/timer task memory */

/* ---- synchronisation primitives ---------------------------------------- */
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            1
#define configUSE_QUEUE_SETS                     0
#define configQUEUE_REGISTRY_SIZE                8
#define configUSE_TASK_NOTIFICATIONS             1
#define configUSE_STREAM_BUFFERS                 1

/* ---- software timers --------------------------------------------------- */
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                 8
#define configTIMER_TASK_STACK_DEPTH             256

/* ---- hooks and diagnostics -------------------------------------------- */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configUSE_MALLOC_FAILED_HOOK             0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_TRACE_FACILITY                 1
#define configUSE_STATS_FORMATTING_FUNCTIONS     0
#define configRECORD_STACK_HIGH_ADDRESS          1

#define configGENERATE_RUN_TIME_STATS            1
#define configRUN_TIME_COUNTER_TYPE              uint32_t
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() bsp_timebase_init()
#define portGET_RUN_TIME_COUNTER_VALUE()         bsp_time_us()

#define configASSERT(x)                                                       \
    do {                                                                      \
        if ((x) == 0) {                                                       \
            app_assert_failed(__FILE__, __LINE__);                            \
        }                                                                     \
    } while (0)

/* ---- Cortex-M interrupt priorities ------------------------------------- */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY      15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY      (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configCHECK_HANDLER_INSTALLATION     1

/* ---- optional API ------------------------------------------------------ */
#define INCLUDE_vTaskPrioritySet                 0
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      0
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_xTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1
#define INCLUDE_xTaskGetCurrentTaskHandle        1
#define INCLUDE_uxTaskGetStackHighWaterMark      1
#define INCLUDE_xTaskGetIdleTaskHandle           1
#define INCLUDE_eTaskGetState                    1
#define INCLUDE_xTimerPendFunctionCall           1
#define INCLUDE_xTaskGetHandle                   1
#define INCLUDE_xEventGroupSetBitFromISR         1

/* ---- map the port's handlers onto the CMSIS vector names --------------- */
#define vPortSVCHandler    SVC_Handler
#define xPortPendSVHandler PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif /* FREERTOS_CONFIG_H */

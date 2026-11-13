/**
 * @file    main.c
 * @brief   Boot sequence: fault bookkeeping, clocks, peripherals, banner, scheduler.
 */
#include <stdio.h>

#include "FreeRTOS.h"
#include "app.h"
#include "bsp.h"
#include "fault.h"
#include "task.h"

int main(void)
{
    fault_boot();          /* boot counter + validate the record from the previous run */
    bsp_clock_init();      /* 84 MHz, or HSI fallback */
    (void)bsp_reset_cause(); /* latch and clear RCC reset flags early */
    bsp_timebase_init();   /* TIM2 1 MHz, needed by the ADC driver and run-time stats */
    bsp_led_init();
    bsp_adc_init();
    bsp_uart_init(115200u);

    char line[128];
    bsp_uart_puts_polled("\r\n\r\n");
    snprintf(line, sizeof line, "%s %s | %s | FreeRTOS %s | %lu MHz%s\r\n", APP_NAME, APP_VERSION, BSP_BOARD_NAME,
             tskKERNEL_VERSION_NUMBER, (unsigned long)(SystemCoreClock / 1000000u),
             bsp_clock_fallback() ? " (PLL fallback)" : "");
    bsp_uart_puts_polled(line);
    snprintf(line, sizeof line, "boot #%lu, reset cause: %s\r\n", (unsigned long)fault_boot_count(),
             bsp_reset_cause_str(bsp_reset_cause()));
    bsp_uart_puts_polled(line);

    app_tasks_create();
    vTaskStartScheduler();

    /* Only reached if the idle or timer task could not be created. */
    fault_fatal(FAULT_ASSERT, "scheduler returned", 0u);
}

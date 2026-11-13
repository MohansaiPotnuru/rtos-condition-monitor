/**
 * @file    bsp.h
 * @brief   Board support for NUCLEO-F401RE (and the QEMU netduinoplus2 stand-in).
 *
 * Pins (NUCLEO-F401RE):
 *   PA2 / PA3  USART2 TX / RX  -> ST-LINK virtual COM port, 115200 8N1
 *   PA0        ADC1_IN0        -> Arduino A0: analog sensor input (vibration, current, pot)
 *   PA5        LD2 (green LED)
 *   PC13       B1 (blue button, active low, EXTI13)
 *   internal   ADC1_IN17 VREFINT, ADC1_IN18 temperature sensor
 *
 * Built with TARGET_QEMU, register blocks that QEMU does not model (RCC,
 * PWR, FLASH, GPIO, IWDG, ADC common) are left alone and the UART transmits
 * by polling; everything above the BSP is identical.
 */
#ifndef PLATFORM_BSP_H
#define PLATFORM_BSP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(TARGET_QEMU)
#define BSP_BOARD_NAME "qemu-netduinoplus2"
#else
#define BSP_BOARD_NAME "NUCLEO-F401RE"
#endif

/* NVIC priorities (0 = highest). Anything calling FreeRTOS FromISR APIs must
 * be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5). */
#define BSP_IRQ_PRIO_UART   6u
#define BSP_IRQ_PRIO_BUTTON 7u

/* ---- clock and reset --------------------------------------------------- */
typedef enum {
    RESET_POWER_ON = 0,
    RESET_PIN,
    RESET_SOFTWARE,
    RESET_IWDG,
    RESET_WWDG,
    RESET_LOW_POWER,
    RESET_BROWN_OUT,
    RESET_UNKNOWN,
} bsp_reset_cause_t;

void              bsp_clock_init(void);           /* 84 MHz from HSI + PLL, with fallback */
bool              bsp_clock_fallback(void);       /* true if the PLL did not lock */
bsp_reset_cause_t bsp_reset_cause(void);          /* read once at boot, then flags cleared */
const char       *bsp_reset_cause_str(bsp_reset_cause_t c);
void              bsp_system_reset(void);

/* ---- microsecond timebase (TIM2, 32-bit, 1 MHz) ------------------------ */
void     bsp_timebase_init(void);
uint32_t bsp_time_us(void);

/* ---- UART (USART2) ------------------------------------------------------ */
void   bsp_uart_init(uint32_t baud);             /* call before the scheduler starts */
/* Thread-safe; blocks for up to @p timeout_ms while the TX buffer is full. */
size_t bsp_uart_write(const void *data, size_t len, uint32_t timeout_ms);
void   bsp_uart_puts(const char *s);
size_t bsp_uart_read(uint8_t *buf, size_t max, uint32_t timeout_ms);
/* Polled, interrupt-free output for boot banners and fault handlers. */
void   bsp_uart_puts_polled(const char *s);

typedef struct {
    uint32_t rx_overrun;   /* hardware ORE: a byte was lost before the ISR ran */
    uint32_t rx_dropped;   /* RX stream buffer full */
    uint32_t tx_dropped;   /* bytes not queued before the timeout */
} bsp_uart_stats_t;
void bsp_uart_get_stats(bsp_uart_stats_t *s);

/* ---- ADC1 --------------------------------------------------------------- */
#define BSP_ADC_CH_AIN     0u
#define BSP_ADC_CH_VREFINT 17u
#define BSP_ADC_CH_TEMP    18u
void     bsp_adc_init(void);
bool     bsp_adc_read(uint8_t channel, uint16_t *out); /* false on conversion timeout */
uint16_t bsp_adc_vrefint_cal(void);                    /* factory VREFINT_CAL, 0 if unavailable */

/* ---- LED and button ----------------------------------------------------- */
void bsp_led_init(void);
void bsp_led_set(bool on);
/* The callback runs in interrupt context (already debounced to 50 ms). */
void bsp_button_init(void (*on_press)(void));

/* ---- independent watchdog ----------------------------------------------- */
void bsp_iwdg_start(uint32_t timeout_ms);
void bsp_iwdg_kick(void);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_BSP_H */

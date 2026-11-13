/**
 * @file    bsp_uart.c
 * @brief   Interrupt-driven USART2 with FreeRTOS stream buffers.
 *
 *   tasks --(mutex)--> TX stream buffer --TXE ISR--> USART2->DR
 *   USART2->DR --RXNE ISR--> RX stream buffer --> CLI task
 *
 * A stream buffer is single-writer / single-reader, so writers serialise on a
 * mutex (priority inheritance bounds the blocking a low-priority holder can
 * cause, which is the B term in the response-time analysis). The ISR is the
 * single reader of TX and the single writer of RX. Everything is statically
 * allocated.
 */
#include <string.h>

#include "FreeRTOS.h"
#include "bsp.h"
#include "semphr.h"
#include "stm32f4xx.h"
#include "stream_buffer.h"

#define TX_BUF_SIZE 1024u
#define RX_BUF_SIZE 128u

uint32_t bsp_apb1_hz(void);

static StaticStreamBuffer_t s_tx_ctrl, s_rx_ctrl;
static uint8_t              s_tx_store[TX_BUF_SIZE + 1u];
static uint8_t              s_rx_store[RX_BUF_SIZE + 1u];
static StreamBufferHandle_t s_tx, s_rx;
static StaticSemaphore_t    s_tx_mutex_buf;
static SemaphoreHandle_t    s_tx_mutex;
static volatile bsp_uart_stats_t s_stats;

void bsp_uart_init(uint32_t baud)
{
    s_tx       = xStreamBufferCreateStatic(TX_BUF_SIZE, 1u, s_tx_store, &s_tx_ctrl);
    s_rx       = xStreamBufferCreateStatic(RX_BUF_SIZE, 1u, s_rx_store, &s_rx_ctrl);
    s_tx_mutex = xSemaphoreCreateMutexStatic(&s_tx_mutex_buf);
    configASSERT(s_tx != NULL && s_rx != NULL && s_tx_mutex != NULL);
    vQueueAddToRegistry(s_tx_mutex, "uart_tx");

#if !defined(TARGET_QEMU)
    /* PA2 = TX, PA3 = RX, alternate function 7 */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER = (GPIOA->MODER & ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3)) |
                   (GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1);
    GPIOA->AFR[0] = (GPIOA->AFR[0] & ~((0xFu << 8) | (0xFu << 12))) | (7u << 8) | (7u << 12);
    GPIOA->PUPDR  = (GPIOA->PUPDR & ~GPIO_PUPDR_PUPD3) | GPIO_PUPDR_PUPD3_0; /* RX pull-up */
#endif
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    (void)RCC->APB1ENR;

    USART2->CR1 = 0u;
    /* Oversampling by 16: BRR = f_pclk / baud, rounded (mantissa.fraction in one go). */
    const uint32_t pclk = bsp_apb1_hz();
    USART2->BRR = (pclk + baud / 2u) / baud;
    USART2->CR2 = 0u;
    USART2->CR3 = 0u;
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;

    NVIC_SetPriority(USART2_IRQn, BSP_IRQ_PRIO_UART);
    NVIC_EnableIRQ(USART2_IRQn);
}

void bsp_uart_puts_polled(const char *s)
{
    while (*s != '\0') {
        uint32_t guard = 100000u;
        while ((USART2->SR & USART_SR_TXE) == 0u && --guard != 0u) {
        }
        USART2->DR = (uint8_t)*s++;
    }
}

size_t bsp_uart_write(const void *data, size_t len, uint32_t timeout_ms)
{
    const uint8_t *p    = (const uint8_t *)data;
    size_t         sent = 0u;

    if (len == 0u) {
        return 0u;
    }
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        s_stats.tx_dropped += len;
        return 0u;
    }
#if defined(TARGET_QEMU)
    /* QEMU's USART model completes writes synchronously, so poll. */
    for (; sent < len; sent++) {
        USART2->DR = p[sent];
    }
#else
    const TickType_t start = xTaskGetTickCount();
    const TickType_t limit = pdMS_TO_TICKS(timeout_ms);
    while (sent < len) {
        const TickType_t waited = xTaskGetTickCount() - start;
        if (waited > limit) {
            break;
        }
        sent += xStreamBufferSend(s_tx, p + sent, len - sent, limit - waited);
        /* Kick the transmitter. CR1 is also modified by the ISR, so do the
         * read-modify-write with the UART interrupt masked. */
        taskENTER_CRITICAL();
        USART2->CR1 |= USART_CR1_TXEIE;
        taskEXIT_CRITICAL();
    }
    if (sent < len) {
        s_stats.tx_dropped += (uint32_t)(len - sent);
    }
#endif
    xSemaphoreGive(s_tx_mutex);
    return sent;
}

void bsp_uart_puts(const char *s)
{
    (void)bsp_uart_write(s, strlen(s), 200u);
}

size_t bsp_uart_read(uint8_t *buf, size_t max, uint32_t timeout_ms)
{
    return xStreamBufferReceive(s_rx, buf, max, pdMS_TO_TICKS(timeout_ms));
}

void bsp_uart_get_stats(bsp_uart_stats_t *s)
{
    taskENTER_CRITICAL();
    s->rx_overrun = s_stats.rx_overrun;
    s->rx_dropped = s_stats.rx_dropped;
    s->tx_dropped = s_stats.tx_dropped;
    taskEXIT_CRITICAL();
}

void USART2_IRQHandler(void)
{
    BaseType_t     woken = pdFALSE;
    const uint32_t sr    = USART2->SR;

    if (sr & (USART_SR_RXNE | USART_SR_ORE)) {
        /* Reading DR after SR clears RXNE and ORE. */
        const uint8_t b = (uint8_t)USART2->DR;
        if (sr & USART_SR_ORE) {
            s_stats.rx_overrun++;
        }
        if (xStreamBufferSendFromISR(s_rx, &b, 1u, &woken) != 1u) {
            s_stats.rx_dropped++;
        }
    }

    if ((sr & USART_SR_TXE) && (USART2->CR1 & USART_CR1_TXEIE)) {
        uint8_t b;
        if (xStreamBufferReceiveFromISR(s_tx, &b, 1u, &woken) == 1u) {
            USART2->DR = b;
        } else {
            USART2->CR1 &= ~USART_CR1_TXEIE; /* drained: stop TXE interrupts */
        }
    }
    portYIELD_FROM_ISR(woken);
}

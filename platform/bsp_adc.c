/**
 * @file    bsp_adc.c
 * @brief   ADC1 single conversions: PA0 (IN0), VREFINT (IN17), temperature (IN18).
 *
 * ADCCLK = PCLK2 / 4 = 21 MHz (max 36 MHz). The internal channels need at
 * least 10 us of sampling time, so they use 480 cycles (22.9 us); PA0 uses
 * 84 cycles (4 us) for a source impedance up to ~20 kOhm. One full sensor
 * cycle (3 conversions) costs ~55 us, about 0.6 % of the 10 ms period.
 */
#include "bsp.h"

#include "stm32f4xx.h"

#define SMP_84_CYCLES  4u
#define SMP_480_CYCLES 7u

/* Factory VREFINT calibration (raw code at VDDA = 3.3 V, 30 degC), DS10086 table 72. */
#define VREFINT_CAL_ADDR ((const volatile uint16_t *)0x1FFF7A2Au)

void bsp_adc_init(void)
{
#if !defined(TARGET_QEMU)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;
    GPIOA->MODER |= GPIO_MODER_MODER0; /* PA0 analog mode */
#endif
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    (void)RCC->APB2ENR;

#if !defined(TARGET_QEMU)
    /* Common: PCLK2/4 and enable the temperature sensor + VREFINT. */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0 | ADC_CCR_TSVREFE;
#endif

    ADC1->CR1   = 0u;                        /* 12-bit, no scan, no interrupts */
    ADC1->CR2   = 0u;                        /* single conversion, right aligned */
    ADC1->SQR1  = 0u;                        /* one conversion per sequence */
    ADC1->SMPR2 = (SMP_84_CYCLES << ADC_SMPR2_SMP0_Pos);
    ADC1->SMPR1 = (SMP_480_CYCLES << ADC_SMPR1_SMP17_Pos) | (SMP_480_CYCLES << ADC_SMPR1_SMP18_Pos);
    ADC1->CR2 |= ADC_CR2_ADON;

    /* tSTAB of the ADC (3 us) and the temperature sensor (10 us) */
    const uint32_t t0 = bsp_time_us();
    while ((bsp_time_us() - t0) < 20u) {
    }
}

bool bsp_adc_read(uint8_t channel, uint16_t *out)
{
    ADC1->SR   = 0u;
    ADC1->SQR3 = channel;
    ADC1->CR2 |= ADC_CR2_SWSTART;

#if defined(TARGET_QEMU)
    /* QEMU's STM32F2xx ADC model never sets EOC; it synthesises a ramp on the
     * DR read that follows SWSTART. Read it directly so the whole processing
     * chain (filters, RMS windows, alarms, telemetry) is exercised in CI. */
    *out = (uint16_t)(ADC1->DR & 0x0FFFu);
    return true;
#endif
    /* 480 + 12 cycles at 21 MHz is ~23 us; give up after 200 us. */
    const uint32_t t0 = bsp_time_us();
    while ((ADC1->SR & ADC_SR_EOC) == 0u) {
        if ((bsp_time_us() - t0) > 200u) {
            return false;
        }
    }
    *out = (uint16_t)(ADC1->DR & 0x0FFFu); /* reading DR clears EOC */
    return true;
}

uint16_t bsp_adc_vrefint_cal(void)
{
#if defined(TARGET_QEMU)
    return 0u; /* system memory is not mapped in QEMU: use the typical value */
#else
    const uint16_t cal = *VREFINT_CAL_ADDR;
    /* Sanity window: typical 1489 (1.2 V at 3.3 V). Reject blank/erased words. */
    return (cal > 1300u && cal < 1700u) ? cal : 0u;
#endif
}

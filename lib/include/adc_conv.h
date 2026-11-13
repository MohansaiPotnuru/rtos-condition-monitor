/**
 * @file    adc_conv.h
 * @brief   Ratiometric ADC conversions for the STM32F4 12-bit ADC.
 *
 * VDDA is not assumed to be 3.3 V: it is measured every cycle from the
 * internal reference (VREFINT, ~1.21 V) using the factory calibration word
 * VREFINT_CAL, which ST measures at VDDA = 3.3 V. Every other channel is then
 * scaled by the real VDDA, so readings stay correct on battery or a sagging
 * USB supply.
 *
 * Die temperature uses the typical RM0368 / DS10086 parameters
 * (V25 = 760 mV, Avg_Slope = 2.5 mV/degC). Absolute accuracy is about
 * +/-1.5 degC: good for trend and over-temperature alarms, not for metrology.
 */
#ifndef LIB_ADC_CONV_H
#define LIB_ADC_CONV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_FULL_SCALE      4095u
#define ADC_CAL_VDDA_MV     3300u
#define ADC_VREFINT_TYP_MV  1210u  /* used when no calibration word is available */
#define ADC_TS_V25_MV       760
#define ADC_TS_SLOPE_UV     2500   /* 2.5 mV/degC */

/** VDDA in mV from a VREFINT sample. @p vrefint_cal == 0 selects the typical value. */
uint32_t adc_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal);

/** Raw code to millivolts, rounded, for a given VDDA. */
uint32_t adc_raw_to_mv(uint16_t raw, uint32_t vdda_mv);

/** Temperature-sensor voltage to centi-degrees Celsius (2534 == 25.34 degC). */
int32_t adc_temp_cdeg(uint32_t sense_mv);

#ifdef __cplusplus
}
#endif

#endif /* LIB_ADC_CONV_H */

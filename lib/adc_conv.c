#include "adc_conv.h"

uint32_t adc_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal)
{
    if (vrefint_raw == 0u) {
        return 0u;
    }
    if (vrefint_cal == 0u) {
        /* VDDA = VREF_typ * 4095 / raw */
        return (ADC_VREFINT_TYP_MV * ADC_FULL_SCALE + vrefint_raw / 2u) / vrefint_raw;
    }
    /* VDDA = 3.3 V * CAL / raw (CAL was taken at 3.3 V) */
    return (ADC_CAL_VDDA_MV * (uint32_t)vrefint_cal + vrefint_raw / 2u) / vrefint_raw;
}

uint32_t adc_raw_to_mv(uint16_t raw, uint32_t vdda_mv)
{
    return ((uint32_t)raw * vdda_mv + ADC_FULL_SCALE / 2u) / ADC_FULL_SCALE;
}

int32_t adc_temp_cdeg(uint32_t sense_mv)
{
    /* T = (V - V25) / slope + 25  ->  in centi-degC with the slope in uV/degC */
    const int32_t dv_uv = ((int32_t)sense_mv - ADC_TS_V25_MV) * 1000;
    const int32_t num   = dv_uv * 100;
    /* Round to nearest, symmetric for negative values. */
    const int32_t q = (num >= 0) ? (num + ADC_TS_SLOPE_UV / 2) / ADC_TS_SLOPE_UV
                                 : -((-num + ADC_TS_SLOPE_UV / 2) / ADC_TS_SLOPE_UV);
    return q + 2500;
}

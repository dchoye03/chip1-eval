/**
  ******************************************************************************
  * @file    dac_internal.h
  * @brief   STM32H533 internal DAC1 driver (SPEC.md v4 §DAC).
  *          ch1 = PA4 (DAC1_OUT1), ch2 = PA5 (DAC1_OUT2), 12-bit, buffered.
  *
  *          Per-channel calibration (jig correction):
  *            uV_corrected = (uV + offset_uV) * (1 + gain_ppm / 1e6)
  *          applied before the uV -> code conversion. Runtime only
  *          (reset on reboot).
  ******************************************************************************
  */
#ifndef DAC_INTERNAL_H
#define DAC_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DAC_INTERNAL_OK          0
#define DAC_INTERNAL_ERR_RANGE  (-1)   /* requested voltage > VDDA */

#define DAC_INTERNAL_VDDA_UV  3300000U   /* VDDA = 3.3V 가정 (실측 TODO) */

typedef struct
{
  int32_t offset_uv;   /* default 0 */
  int32_t gain_ppm;    /* default 0 = x1.0 */
} dac_cal_t;

/* Enable DAC1 clock, PA4/PA5 analog, buffered outputs, both channels on, 0V. */
void dac_internal_init(void);

/* Output on ch (1|2). Applies calibration, then code = round(uV*4095/VDDA).
   Returns DAC_INTERNAL_ERR_RANGE if uv > VDDA (corrected value is clamped). */
int dac_internal_set_uv(uint8_t ch, uint32_t uv);

/* Calibration coefficients ('dac cal'). ch = 1|2. */
void dac_internal_set_cal(uint8_t ch, int32_t offset_uv, int32_t gain_ppm);
const dac_cal_t *dac_internal_get_cal(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* DAC_INTERNAL_H */

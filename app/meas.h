/**
  ******************************************************************************
  * @file    meas.h
  * @brief   Internal ADC measurement for the temperature-sweep setup:
  *          DUT DVDD node + VDDA/REFIN node, VREFINT-corrected (SPEC.md §8).
  *
  *          'meas' CLI command -> "VDD=3.299V VREF=3.001V" + OK
  ******************************************************************************
  */
#ifndef MEAS_H
#define MEAS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MEAS_OK          0
#define MEAS_ERR_INIT   (-1)
#define MEAS_ERR_CONV   (-2)

#define MEAS_AVG_COUNT  16U   /* samples averaged per node */

/* Read both nodes in millivolts. VDDA is measured each call via VREFINT
   (factory cal @3.3V) so the result does not assume VDDA == 3.3V.
   Lazy-inits the ADC on first call. */
int meas_read(uint32_t *vdd_mv, uint32_t *vref_mv);

/* Self-calibration ('meas cal'): re-runs the ADC hardware calibration
   (ADCAL - offset) and re-references VDDA via VREFINT (gain). H5 has no
   VSSA internal channel, so ADCAL substitutes the 0V point.
   Returns the calibration factor, VREFINT raw (16-avg) and computed VDDA. */
int meas_selfcal(uint32_t *calfactor, uint32_t *vrefint_raw, uint32_t *vdda_mv);

#ifdef __cplusplus
}
#endif

#endif /* MEAS_H */

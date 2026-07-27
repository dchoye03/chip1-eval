/**
  ******************************************************************************
  * @file    delay.h
  * @brief   Millisecond / microsecond blocking delays.
  *
  *          delay_ms wraps HAL_Delay (SysTick based).
  *          delay_us uses the DWT cycle counter - needed later for driver
  *          bit-timing (e.g. DAC1220 self-calibration waits, CS setup times).
  ******************************************************************************
  */
#ifndef DELAY_H
#define DELAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Enable the DWT cycle counter. Call once at startup, before delay_us. */
void delay_init(void);

/* Blocking delay in milliseconds (SysTick / HAL_Delay). */
void delay_ms(uint32_t ms);

/* Blocking delay in microseconds (DWT cycle counter, busy-wait). */
void delay_us(uint32_t us);

#ifdef __cplusplus
}
#endif

#endif /* DELAY_H */

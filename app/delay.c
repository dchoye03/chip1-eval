/**
  ******************************************************************************
  * @file    delay.c
  * @brief   Millisecond / microsecond blocking delays.
  ******************************************************************************
  */
#include "delay.h"
#include "stm32h5xx_hal.h"

void delay_init(void)
{
  /* Enable trace subsystem, then the DWT cycle counter */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void delay_ms(uint32_t ms)
{
  HAL_Delay(ms);
}

void delay_us(uint32_t us)
{
  const uint32_t cycles_per_us = SystemCoreClock / 1000000U;
  const uint32_t start = DWT->CYCCNT;
  const uint32_t target = us * cycles_per_us;

  while ((DWT->CYCCNT - start) < target)
  {
    /* busy wait */
  }
}

/**
  ******************************************************************************
  * @file    meas.c
  * @brief   Internal ADC1 measurement (HAL ADC, single conversions).
  *
  *          VDDA correction: VREFINT is measured against the factory
  *          calibration value (read @ VDDA=3.3V, address VREFINT_CAL_ADDR),
  *          so VDDA_actual = 3300mV * CAL / raw. Node voltages are then
  *          computed against the measured VDDA - no 3.3V assumption.
  ******************************************************************************
  */
#include "meas.h"

#include "stm32h5xx_hal.h"
#include "board_pins.h"

static ADC_HandleTypeDef hadc1;
static uint8_t meas_ready = 0;
static uint16_t vrefint_cal = 0;   /* 공장 캘값 캐시 (아래 ICACHE 주의 참조) */

static int meas_init(void)
{
  GPIO_InitTypeDef gi = {0};

  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_ADC_CLK_ENABLE();

  gi.Mode = GPIO_MODE_ANALOG;
  gi.Pull = GPIO_NOPULL;
  gi.Pin  = MEAS_VDD_PIN;   HAL_GPIO_Init(MEAS_VDD_PORT, &gi);
  gi.Pin  = MEAS_VREF_PIN;  HAL_GPIO_Init(MEAS_VREF_PORT, &gi);

  hadc1.Instance                   = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV4;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.NbrOfConversion       = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;

  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    return MEAS_ERR_INIT;
  }
  if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
  {
    return MEAS_ERR_INIT;
  }

  /* ⚠ H5 함정: ICACHE가 켜진 상태에서 엔지니어링 바이트 영역
     (VREFINT_CAL_ADDR = 0x08FFF810)을 읽으면 정밀 버스폴트 -> HardFault.
     (실측: 2026-07-22, BFAR=0x08FFF810로 확인) 캘값을 읽는 동안만 ICACHE를
     끄고, 한 번 읽어 캐시해둔다. __LL_ADC_CALC_VREFANALOG_VOLTAGE 매크로는
     내부에서 이 주소를 직접 읽으므로 사용 금지 - 수동 계산할 것. */
  HAL_ICACHE_Disable();
  vrefint_cal = *VREFINT_CAL_ADDR;
  HAL_ICACHE_Enable();
  if (vrefint_cal == 0U || vrefint_cal == 0xFFFFU)
  {
    return MEAS_ERR_INIT;   /* 캘값 비정상 */
  }

  meas_ready = 1;
  return MEAS_OK;
}

/* One channel, MEAS_AVG_COUNT-sample average. Long sampling time so the
   same config also satisfies VREFINT's minimum sampling time. */
static int read_avg(uint32_t channel, uint32_t *avg_out)
{
  ADC_ChannelConfTypeDef cfg = {0};
  uint32_t sum = 0;

  cfg.Channel      = channel;
  cfg.Rank         = ADC_REGULAR_RANK_1;
  cfg.SamplingTime = ADC_SAMPLETIME_247CYCLES_5;
  cfg.SingleDiff   = ADC_SINGLE_ENDED;
  cfg.OffsetNumber = ADC_OFFSET_NONE;
  cfg.Offset       = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK)
  {
    return MEAS_ERR_CONV;
  }

  for (uint32_t i = 0; i < MEAS_AVG_COUNT; i++)
  {
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
      return MEAS_ERR_CONV;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK)
    {
      HAL_ADC_Stop(&hadc1);
      return MEAS_ERR_CONV;
    }
    sum += HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
  }

  *avg_out = (sum + MEAS_AVG_COUNT / 2U) / MEAS_AVG_COUNT;
  return MEAS_OK;
}

int meas_selfcal(uint32_t *calfactor, uint32_t *vrefint_raw, uint32_t *vdda_mv)
{
  int rc;

  if (!meas_ready)
  {
    rc = meas_init();       /* init 경로에 이미 최초 ADCAL 포함 */
    if (rc != MEAS_OK)
    {
      return rc;
    }
  }
  else
  {
    /* ADC 하드웨어 캘리브레이션 재실행 (오프셋 셀프캘, 온도 드리프트 대응).
       HAL_ADC_Stop이 매 변환 후 ADC를 disable하므로 바로 실행 가능. */
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK)
    {
      return MEAS_ERR_INIT;
    }
  }

  rc = read_avg(ADC_CHANNEL_VREFINT, vrefint_raw);
  if (rc != MEAS_OK || *vrefint_raw == 0U)
  {
    return MEAS_ERR_CONV;
  }
  *vdda_mv = (VREFINT_CAL_VREF * (uint32_t)vrefint_cal) / *vrefint_raw;
  *calfactor = HAL_ADCEx_Calibration_GetValue(&hadc1, ADC_SINGLE_ENDED);
  return MEAS_OK;
}

int meas_read(uint32_t *vdd_mv, uint32_t *vref_mv)
{
  uint32_t vrefint_raw, vdd_raw, vref_raw;
  int rc;

  if (!meas_ready)
  {
    rc = meas_init();
    if (rc != MEAS_OK)
    {
      return rc;
    }
  }

  /* 1. VREFINT -> actual VDDA (factory cal taken at 3.3V).
     수동 계산 - HAL 매크로는 매번 캘 주소를 직접 읽어 ICACHE 폴트 유발
     (meas_init의 주의 참조). VDDA = 3300mV * CAL / raw */
  rc = read_avg(ADC_CHANNEL_VREFINT, &vrefint_raw);
  if (rc != MEAS_OK || vrefint_raw == 0U)
  {
    return MEAS_ERR_CONV;
  }
  uint32_t vdda_mv = (VREFINT_CAL_VREF * (uint32_t)vrefint_cal) / vrefint_raw;

  /* 2. Nodes, scaled against measured VDDA */
  rc = read_avg(MEAS_VDD_CHANNEL, &vdd_raw);
  if (rc != MEAS_OK)
  {
    return rc;
  }
  rc = read_avg(MEAS_VREF_CHANNEL, &vref_raw);
  if (rc != MEAS_OK)
  {
    return rc;
  }

  *vdd_mv  = __LL_ADC_CALC_DATA_TO_VOLTAGE(vdda_mv, vdd_raw,
                                           LL_ADC_RESOLUTION_12B);
  *vref_mv = __LL_ADC_CALC_DATA_TO_VOLTAGE(vdda_mv, vref_raw,
                                           LL_ADC_RESOLUTION_12B);
  return MEAS_OK;
}

/**
  ******************************************************************************
  * @file    dac_internal.c
  * @brief   STM32H533 internal DAC1 driver - direct register access.
  *
  *          HAL DAC module is not enabled in this project; the DAC needs only
  *          three registers (MCR mode, CR enable, DHR12Rx data), so we drive
  *          them directly via CMSIS instead of pulling in the HAL module.
  ******************************************************************************
  */
#include "dac_internal.h"

#include "stm32h5xx_hal.h"
#include "board_pins.h"
#include "cal_store.h"
#include "delay.h"

/* [0]=ch1, [1]=ch2; zero-init = no correction.
   영구화는 PC측에서 해결: 2점 캘리브레이션 위저드(tools/chip1_gui.pyw)가
   dac_cal.json에 저장하고, 테스트 시작 시 'dac cal'로 재주입한다 (2026-07-21).
   펌웨어 플래시 저장은 불필요해져 보류. */
static dac_cal_t cal[2];

void dac_internal_init(void)
{
  GPIO_InitTypeDef gi = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_DAC1_CLK_ENABLE();

  /* PA4/PA5 to analog - PA5 was the user LED, sacrificed for DAC1_OUT2 */
  gi.Pin  = DAC_CH1_PIN | DAC_CH2_PIN;
  gi.Mode = GPIO_MODE_ANALOG;
  gi.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gi);

  /* MODE1/MODE2 = 000: connected to external pin, output buffer enabled */
  DAC1->MCR &= ~(DAC_MCR_MODE1_Msk | DAC_MCR_MODE2_Msk);

  DAC1->CR |= DAC_CR_EN1 | DAC_CR_EN2;
  delay_us(15);            /* tWAKEUP after enable */

  DAC1->DHR12R1 = 0;       /* both channels start at 0V */
  DAC1->DHR12R2 = 0;

  /* 보드 내 저장소(플래시 마지막 섹터)에 캘 계수가 있으면 자동 적용 —
     PC 주입 없이도(어느 PC/터미널이든) 보정된 출력이 나오게 한다.
     채널별 valid 마스크 지원: 저장된 채널만 적용, 나머지는 무보정(0). */
  {
    int32_t off[2] = {0, 0}, ppm[2] = {0, 0};
    (void)cal_store_load(off, ppm);   /* valid 채널만 채워짐 */
    cal[0].offset_uv = off[0]; cal[0].gain_ppm = ppm[0];
    cal[1].offset_uv = off[1]; cal[1].gain_ppm = ppm[1];
  }
}

int dac_internal_set_uv(uint8_t ch, uint32_t uv)
{
  const dac_cal_t *c = &cal[(ch == 2U) ? 1 : 0];

  if (uv > DAC_INTERNAL_VDDA_UV)
  {
    return DAC_INTERNAL_ERR_RANGE;
  }

  /* uV_corrected = (uV + offset) * (1 + gain_ppm/1e6), 64-bit to avoid
     overflow (3.3e6 * ~1e6 fits in int64). Clamp result into 0..VDDA. */
  int64_t corrected = ((int64_t)uv + c->offset_uv) * (1000000LL + c->gain_ppm)
                      / 1000000LL;
  if (corrected < 0)
  {
    corrected = 0;
  }
  if (corrected > (int64_t)DAC_INTERNAL_VDDA_UV)
  {
    corrected = DAC_INTERNAL_VDDA_UV;
  }

  /* code = round(uV * 4095 / VDDA), 0..4095 */
  uint32_t code = (uint32_t)((corrected * 4095 + (DAC_INTERNAL_VDDA_UV / 2))
                             / DAC_INTERNAL_VDDA_UV);
  if (code > 4095U)
  {
    code = 4095U;
  }

  if (ch == 2U)
  {
    DAC1->DHR12R2 = code;
  }
  else
  {
    DAC1->DHR12R1 = code;
  }
  return DAC_INTERNAL_OK;
}

void dac_internal_set_cal(uint8_t ch, int32_t offset_uv, int32_t gain_ppm)
{
  dac_cal_t *c = &cal[(ch == 2U) ? 1 : 0];
  c->offset_uv = offset_uv;
  c->gain_ppm  = gain_ppm;

  /* 2점 자동 보정은 PC측 위저드로 구현됨 (tools/chip1_gui.pyw, 2026-07-21):
     저/고점 출력 → 실측 입력 → offset/gain 산출 → 이 함수로 주입.
     펌웨어 단독 'dac cal auto'는 기준 측정기가 없어 계획 없음. */
}

const dac_cal_t *dac_internal_get_cal(uint8_t ch)
{
  return &cal[(ch == 2U) ? 1 : 0];
}

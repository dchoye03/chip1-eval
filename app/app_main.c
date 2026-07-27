/**
  ******************************************************************************
  * @file    app_main.c
  * @brief   Application setup and main loop.
  *
  *          v4: heartbeat LED 제거 - PA5(LD2)가 DAC1_OUT2로 전용됨.
  *          생존 확인은 UART 프롬프트 응답으로 대신한다.
  ******************************************************************************
  */
#include "app_main.h"

#include "stm32h5xx_nucleo.h"
#include "board_pins.h"
#include "delay.h"
#include "dac_internal.h"
#include "cli.h"

/* CHIP1 instance - pins injected from board_pins.h (the only pin source) */
const chip1_t g_adc = {
  CHIP1_SCK_PORT, CHIP1_SCK_PIN, CHIP1_SDA_PORT, CHIP1_SDA_PIN,
};

void app_setup(void)
{
  delay_init();

  /* CHIP1 SCK must sit LOW as early as possible
     (idle-high >100ms puts the chip into power-down, SPEC.md §3.1) */
  chip1_gpio_init(&g_adc);

  /* DAC를 부팅 시 초기화 → 보드 플래시의 캘 계수가 즉시 로드/적용됨.
     PC나 'dac init' 없이 터미널에서 'dac set'만 쳐도 보정 출력이 나오게. */
  dac_internal_init();

  cli_init(&hcom_uart[COM1]);   /* ST-LINK VCP: USART2 @ 115200 */
}

void app_loop(void)
{
  cli_poll();
}

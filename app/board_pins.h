/**
  ******************************************************************************
  * @file    board_pins.h
  * @brief   All pin assignments for the CHIP1 load-cell eval unit
  *          (NUCLEO-H533RE). The ONLY place pins are defined (SPEC.md §0).
  *
  *  구성 (v3): Nucleo ↔ CHIP1 센서보드 직결, 점퍼선 4가닥
  *    3.3V / GND / SCK / SDA
  *
  *  보드 예약 핀 (사용 금지):
  *    PA2/PA3 (VCP USART2), PA4/PA5 (내장 DAC 출력), PC13 (B1),
  *    PA13/PA14 (SWD), PA15 (JTDI), PB3 (SWO)
  *    (PA5는 원래 LD2 LED — v4에서 DAC1_OUT2 확보 위해 LED 기능 제거)
  ******************************************************************************
  */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h5xx_hal.h"

/* ---------------------------------------------------------------------------
 * On-board (fixed by NUCLEO-H533RE, BSP handles these)
 *   B1 = PC13, VCP = USART2 PA2/PA3 @115200
 *   LD2(PA5)는 v4부터 DAC1_OUT2로 사용 -> LED 기능 제거됨
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * 내장 DAC1 (v4) - 칩 고정 핀이라 변경 불가
 *   ch1 = PA4 (DAC1_OUT1), ch2 = PA5 (DAC1_OUT2)
 *
 * ⚠ 물리 접근성 (MB1814C 회로도 docs/mb1814-h533re-c02-schematic.pdf 확정):
 *   - PA4: 기본 상태에서 어떤 커넥터에도 미배선! SB22(DNF)->ARD_D1(CN9-2),
 *     SB3(DNF)->ST-LINK VCP 뿐이며 둘 다 미실장. 사용하려면 보드 개조 필요:
 *     SB19 제거 + SB22 납땜 -> PA4가 CN9 핀 2(D1)로 나옴.
 *   - PA5: CN5 핀 6 (D13) / CN10 핀 11. LD2 LED가 SB6->R29 47K->Q1 베이스로
 *     물려 있음 (부하 ~70uA, 버퍼드 출력엔 미미. 정밀도 필요 시 SB6 제거).
 * ------------------------------------------------------------------------- */
#define DAC_CH1_PIN   GPIO_PIN_4   /* PA4, DAC1_OUT1 - fixed by silicon */
#define DAC_CH2_PIN   GPIO_PIN_5   /* PA5, DAC1_OUT2 - fixed by silicon */

/* ---------------------------------------------------------------------------
 * CHIP1 센서보드 - SCK + SDA 2선 펄스 프로토콜 (표준 SPI 아님, 비트뱅잉)
 *   SCK: 출력, idle 반드시 Low (100ms 이상 High -> 칩 파워다운!)
 *   SDA: 양방향 단선 (기본 입력). VDD 3.3V 운용이라 FT/레벨시프트 불필요.
 *
 * TODO(wiring): 후보 핀. 전원(CN6 3V3/GND)과 같은 왼쪽 아두이노 헤더 줄에
 * 모이도록 CN8 (A0/A1)을 선택 — 점퍼 4가닥이 한 구역에서 끝남.
 * 실제 배선 후 이 두 줄만 확정/수정하면 됨.
 *   SCK = PA0  (CN8 pin 1, Arduino A0)
 *   SDA = PA1  (CN8 pin 2, Arduino A1)
 * ------------------------------------------------------------------------- */
#define CHIP1_SCK_PORT  GPIOA
#define CHIP1_SCK_PIN   GPIO_PIN_0    /* TODO(wiring): 후보, 배선 후 확정 */
#define CHIP1_SDA_PORT  GPIOA
#define CHIP1_SDA_PIN   GPIO_PIN_1    /* TODO(wiring): 후보, 배선 후 확정 */

/* ---------------------------------------------------------------------------
 * meas 명령용 내장 ADC 입력 (온도 스윕 연동, SPEC.md §8)
 *
 * 배선 확정 (2026-07-23 실측, SCH_CHIP1_R1.2 + C1 개조 기준):
 *   DUT DVDD 노드   = A3 (PB0, ADC1_INP9)   - CN8 pin 4 <- Nucleo 3V3
 *                     (어댑터 DVDD와 동일 넷, 실측 3.335V)
 *   VDDA/REFIN 노드 = A4 (PC1, ADC1_INP11)  - CN8 pin 5 <- 센서보드 실크
 *                     AVDD 구멍 (C1 개조로 REFIN 노드 탭, 기대 3.0V ±0.05)
 * ⚠ VDDA는 칩 내부 LDO 출력(3.0V) - 외부 전원 주입 금지.
 * ⚠ 미개조 센서보드는 이 구멍이 고립 라인 -> VREF 1.2V대 부유가 정상이며
 *   측정 무효 (개조 보드에 마킹 있음. 상세: SPEC.md 섹션 1 C1 개조 기록).
 * 채널 근거: UM3121 I/O assignment "ARD_A3-ADC1_INP9" / "ARD_A4-ADC1_INP11"
 * ------------------------------------------------------------------------- */
#define MEAS_VDD_PORT     GPIOB
#define MEAS_VDD_PIN      GPIO_PIN_0        /* TODO(wiring): 후보, 배선 후 확정 */
#define MEAS_VDD_CHANNEL  ADC_CHANNEL_9

#define MEAS_VREF_PORT    GPIOC
#define MEAS_VREF_PIN     GPIO_PIN_1        /* TODO(wiring): 후보, 배선 후 확정 */
#define MEAS_VREF_CHANNEL ADC_CHANNEL_11

/* TODO(power): 센서보드 전원을 GPIO로 스위칭하는 개선 (SPEC.md §1) -
   현재는 CN6 3V3 항시 공급. 필요해지면 여기에 PWR_EN 핀 추가. */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_PINS_H */

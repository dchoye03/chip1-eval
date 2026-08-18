/**
  ******************************************************************************
  * @file    chip1a_i2c.h
  * @brief   CHIP1A (I2C variant) driver - bit-banged I2C master.
  *
  *          CHIP1A = CHIP1의 I2C 인터페이스 버전 (데이터시트 "Digital
  *          Serial Interface - I2C Interface"). 같은 8핀/같은 레지스터 맵,
  *          핀 5가 SCK 대신 SCL로 동작. 슬레이브 주소 0x2A, fast-mode
  *          (<=400kHz), 바이트 주소 모드.
  *
  *          같은 배선(SCK->SCL, SDA)을 그대로 쓰므로 하드웨어 변경 없음.
  *          핀은 open-drain + 내부 풀업으로 구동 (EVM에 풀업 없어도 저속
  *          비트뱅잉으로 동작 — 불안정하면 외부 4.7K 권장).
  *
  *          ⚠ I2C idle은 두 라인 모두 High. SPI 버전 칩(CHIP1)이 물린
  *          상태에서 iface i2c로 두면 SCK High 유지로 파워다운됨 — iface를
  *          spi로 되돌릴 때 350ms 웨이크업 대기를 수행한다 (cli.c).
  ******************************************************************************
  */
#ifndef CHIP1A_I2C_H
#define CHIP1A_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "chip1.h"      /* chip1_t pin struct + register map 재사용 */

#define CHIP1A_I2C_ADDR   0x2AU   /* 7-bit slave address (datasheet) */

#define CHIP1A_OK          0
#define CHIP1A_ERR_TIMEOUT (-1)   /* DRDY poll timeout / clock stretch stuck */
#define CHIP1A_ERR_NACK    (-2)   /* slave did not ACK (배선/주소/칩 없음) */

/* Configure both lines as open-drain + pull-up, released (idle high). */
void chip1a_gpio_init(const chip1_t *g);

/* Address probe: START, addr+W, ACK 확인, STOP. CHIP1A_OK = ACK 받음.
   'iscan' 명령용 — 실리콘 주소가 데이터시트(0x2A)와 다를 가능성 배제. */
int chip1a_probe(const chip1_t *g, uint8_t addr7);

/* Register write: START, 0x2A+W, reg, val, STOP. */
int chip1a_write_reg(const chip1_t *g, uint8_t reg, uint8_t val);

/* Register read: START, 0x2A+W, reg, RESTART, 0x2A+R, byte, NACK, STOP. */
int chip1a_read_reg(const chip1_t *g, uint8_t reg, uint8_t *val);

/* One sample: poll CFGR DRDY bit (read-clears) until set, then read
   DOUTH/M/L and sign-extend to 32 bit. */
int chip1a_read_sample(const chip1_t *g, int32_t *sample,
                        uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* CHIP1A_I2C_H */

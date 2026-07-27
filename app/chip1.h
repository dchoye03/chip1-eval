/**
  ******************************************************************************
  * @file    chip1.h
  * @brief   CHIP1 24-bit ADC driver - 2-wire pulse protocol (HX711-like).
  *          NOT standard SPI (SPEC.md §4). GPIO bit-bang only.
  *
  *          Critical: SCK idle state MUST be low. SCK held high >100ms
  *          puts the chip into power-down (wake: SCK low + 350ms).
  *
  *          Returns 0 on success, negative CHIP1_ERR_* on failure.
  ******************************************************************************
  */
#ifndef CHIP1_H
#define CHIP1_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32h5xx_hal.h"

#define CHIP1_OK              0
#define CHIP1_ERR_TIMEOUT    (-1)   /* no DRDY (SDA falling) within timeout */

/* Register map (SPEC.md §4.4) */
#define CHIP1_REG_DOUTH   0x00U
#define CHIP1_REG_DOUTM   0x01U
#define CHIP1_REG_DOUTL   0x02U
#define CHIP1_REG_DSMC    0x03U   /* [7:4] CR (rate), [3:0] PGA */
#define CHIP1_REG_CSEL    0x04U   /* [7:5] 000=Diff, 001=Single, 011=Short */
#define CHIP1_REG_CFGR    0x05U
#define CHIP1_REG_RCFG    0x2BU
#define CHIP1_REG_WCFG    0x32U
#define CHIP1_REG_IDH     0x3EU   /* == 0x92, 통신 검증용 */
#define CHIP1_REG_IDL     0x3FU   /* == 0x10 */

#define CHIP1_ID_H        0x92U
#define CHIP1_ID_L        0x10U

/* Per-sample DRDY timeout (spec: 500ms) */
#define CHIP1_DRDY_TIMEOUT_MS  500U

typedef struct
{
  GPIO_TypeDef *sck_port;
  uint16_t      sck_pin;
  GPIO_TypeDef *sda_port;
  uint16_t      sda_pin;
} chip1_t;

/* Configure GPIOs: SCK output LOW (never leave high!), SDA input. */
void chip1_gpio_init(const chip1_t *g);

/* One sample: wait DRDY (SDA falling) -> 27-pulse frame -> sign-extended
   24-bit value. st0 (config-applied flag) is returned if non-NULL. */
int chip1_read_sample(const chip1_t *g, int32_t *sample, uint8_t *st0,
                       uint32_t timeout_ms);

/* Register write ('wr'): one full frame = sample read (discarded) + register
   access tail (pulses 28..46). Config applies from the NEXT conversion. */
int chip1_write_reg(const chip1_t *g, uint8_t addr, uint8_t val,
                     uint32_t timeout_ms);

/* Register read ('rr'): same frame shape, chip drives the 8 data bits. */
int chip1_read_reg(const chip1_t *g, uint8_t addr, uint8_t *val,
                    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* CHIP1_H */

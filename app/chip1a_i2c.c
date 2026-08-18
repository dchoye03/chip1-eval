/**
  ******************************************************************************
  * @file    chip1a_i2c.c
  * @brief   CHIP1A (I2C variant) - bit-banged I2C master implementation.
  *
  *          ~100kHz (half-bit 5us) — fast-mode 상한 400kHz 대비 보수적.
  *          클럭 스트레칭 지원: SCL 해제 후 실제 High 될 때까지 대기.
  ******************************************************************************
  */
#include "chip1a_i2c.h"

#include "stm32h5xx_hal.h"
#include "delay.h"

/* 50kHz — SDA는 내부 풀업(~40k)만으로 뜨므로 상승 시간 여유 확보 */
#define HALF_BIT_US       10U

/* line helpers: open-drain — ODR 1 = released(pull-up이 High로), 0 = drive low */
static inline void scl_low(const chip1_t *g)
{
  HAL_GPIO_WritePin(g->sck_port, g->sck_pin, GPIO_PIN_RESET);
}

static inline void sda_low(const chip1_t *g)
{
  HAL_GPIO_WritePin(g->sda_port, g->sda_pin, GPIO_PIN_RESET);
}

static inline void sda_release(const chip1_t *g)
{
  HAL_GPIO_WritePin(g->sda_port, g->sda_pin, GPIO_PIN_SET);
}

static inline uint8_t sda_read(const chip1_t *g)
{
  return (HAL_GPIO_ReadPin(g->sda_port, g->sda_pin) == GPIO_PIN_SET) ? 1U : 0U;
}

/* SCL High. 푸시풀 구동 — EVM의 R14(47k, SCK→GND 풀다운)가 내부 풀업(~40k)
   을 이겨 SCL이 ~1.8V에서 멈추는 문제의 소프트웨어 해결책 (외부 풀업 불요).
   단일 마스터 + 클럭 스트레칭 없는 슬레이브 전제. 만약의 슬레이브 SCL 충돌은
   EVM의 직렬 100Ω(R13)이 전류 제한. (스트레칭 필요 판명 시 외부 4.7k 풀업
   + 오픈드레인 복귀로 전환할 것.) */
static int scl_release_wait(const chip1_t *g)
{
  HAL_GPIO_WritePin(g->sck_port, g->sck_pin, GPIO_PIN_SET);
  return CHIP1A_OK;
}

void chip1a_gpio_init(const chip1_t *g)
{
  GPIO_InitTypeDef gi = {0};

  /* released(High)로 시작 — 라인 글리치 방지 위해 ODR 먼저 세팅 */
  HAL_GPIO_WritePin(g->sck_port, g->sck_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(g->sda_port, g->sda_pin, GPIO_PIN_SET);

  /* SCL = 푸시풀 (EVM R14 47k 풀다운을 이기기 위해 — scl_release_wait 주석) */
  gi.Mode  = GPIO_MODE_OUTPUT_PP;
  gi.Pull  = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  gi.Pin   = g->sck_pin;
  HAL_GPIO_Init(g->sck_port, &gi);

  /* SDA = 오픈드레인 + 내부 풀업 (슬레이브가 ACK/데이터로 몰아야 하므로) */
  gi.Mode = GPIO_MODE_OUTPUT_OD;
  gi.Pull = GPIO_PULLUP;
  gi.Pin  = g->sda_pin;
  HAL_GPIO_Init(g->sda_port, &gi);

  delay_us(10);
}

static int i2c_start(const chip1_t *g)
{
  /* idle(둘 다 High)에서 SDA를 SCL High 중에 내리면 START */
  sda_release(g);
  int rc = scl_release_wait(g);
  if (rc != CHIP1A_OK)
  {
    return rc;
  }
  delay_us(HALF_BIT_US);
  sda_low(g);
  delay_us(HALF_BIT_US);
  scl_low(g);
  delay_us(HALF_BIT_US);
  return CHIP1A_OK;
}

static int i2c_stop(const chip1_t *g)
{
  sda_low(g);
  delay_us(HALF_BIT_US);
  int rc = scl_release_wait(g);
  if (rc != CHIP1A_OK)
  {
    return rc;
  }
  delay_us(HALF_BIT_US);
  sda_release(g);
  delay_us(HALF_BIT_US);
  return CHIP1A_OK;
}

/* 1바이트 송신 + ACK 확인. CHIP1A_OK / ERR_NACK / ERR_TIMEOUT */
static int i2c_write_byte(const chip1_t *g, uint8_t byte)
{
  for (int i = 7; i >= 0; i--)
  {
    if ((byte >> i) & 1U)
    {
      sda_release(g);
    }
    else
    {
      sda_low(g);
    }
    delay_us(HALF_BIT_US);
    int rc = scl_release_wait(g);
    if (rc != CHIP1A_OK)
    {
      return rc;
    }
    delay_us(HALF_BIT_US);
    scl_low(g);
  }

  /* ACK 슬롯: SDA 해제 후 슬레이브가 Low로 당기면 ACK */
  sda_release(g);
  delay_us(HALF_BIT_US);
  int rc = scl_release_wait(g);
  if (rc != CHIP1A_OK)
  {
    return rc;
  }
  delay_us(HALF_BIT_US / 2U);
  uint8_t ack = (sda_read(g) == 0U) ? 1U : 0U;
  delay_us(HALF_BIT_US / 2U);
  scl_low(g);
  delay_us(HALF_BIT_US);

  return ack ? CHIP1A_OK : CHIP1A_ERR_NACK;
}

/* 1바이트 수신. ack_out=1이면 ACK(계속), 0이면 NACK(마지막 바이트) 송신 */
static int i2c_read_byte(const chip1_t *g, uint8_t *byte, uint8_t ack_out)
{
  uint8_t v = 0;

  sda_release(g);   /* 슬레이브가 데이터를 몰 수 있게 해제 */
  for (int i = 7; i >= 0; i--)
  {
    delay_us(HALF_BIT_US);
    int rc = scl_release_wait(g);
    if (rc != CHIP1A_OK)
    {
      return rc;
    }
    delay_us(HALF_BIT_US / 2U);
    v = (uint8_t)((v << 1) | sda_read(g));
    delay_us(HALF_BIT_US / 2U);
    scl_low(g);
  }

  if (ack_out)
  {
    sda_low(g);
  }
  else
  {
    sda_release(g);
  }
  delay_us(HALF_BIT_US);
  int rc = scl_release_wait(g);
  if (rc != CHIP1A_OK)
  {
    return rc;
  }
  delay_us(HALF_BIT_US);
  scl_low(g);
  sda_release(g);
  delay_us(HALF_BIT_US);

  *byte = v;
  return CHIP1A_OK;
}

int chip1a_probe(const chip1_t *g, uint8_t addr7)
{
  int rc = i2c_start(g);
  if (rc == CHIP1A_OK)
  {
    rc = i2c_write_byte(g, (uint8_t)(addr7 << 1));   /* +W, ACK만 확인 */
  }
  (void)i2c_stop(g);
  return rc;
}

int chip1a_write_reg(const chip1_t *g, uint8_t reg, uint8_t val)
{
  int rc = i2c_start(g);
  if (rc == CHIP1A_OK)
  {
    rc = i2c_write_byte(g, (uint8_t)(CHIP1A_I2C_ADDR << 1));       /* +W */
  }
  if (rc == CHIP1A_OK)
  {
    rc = i2c_write_byte(g, reg);
  }
  if (rc == CHIP1A_OK)
  {
    rc = i2c_write_byte(g, val);
  }
  (void)i2c_stop(g);   /* 에러여도 버스는 idle로 복귀 */
  return rc;
}

int chip1a_read_reg(const chip1_t *g, uint8_t reg, uint8_t *val)
{
  int rc = i2c_start(g);
  if (rc == CHIP1A_OK)
  {
    rc = i2c_write_byte(g, (uint8_t)(CHIP1A_I2C_ADDR << 1));       /* +W */
  }
  if (rc == CHIP1A_OK)
  {
    rc = i2c_write_byte(g, reg);
  }
  if (rc == CHIP1A_OK)
  {
    rc = i2c_start(g);   /* repeated START */
  }
  if (rc == CHIP1A_OK)
  {
    rc = i2c_write_byte(g, (uint8_t)((CHIP1A_I2C_ADDR << 1) | 1U)); /* +R */
  }
  if (rc == CHIP1A_OK)
  {
    rc = i2c_read_byte(g, val, 0U);   /* single byte -> NACK */
  }
  (void)i2c_stop(g);
  return rc;
}

int chip1a_read_sample(const chip1_t *g, int32_t *sample,
                        uint32_t timeout_ms)
{
  uint32_t t0 = HAL_GetTick();
  uint8_t  cfgr = 0;

  /* DRDY = CFGR bit0 (읽으면 클리어) 폴링 */
  for (;;)
  {
    int rc = chip1a_read_reg(g, CHIP1_REG_CFGR, &cfgr);
    if (rc != CHIP1A_OK)
    {
      return rc;
    }
    if ((cfgr & 0x01U) != 0U)
    {
      break;
    }
    if ((HAL_GetTick() - t0) >= timeout_ms)
    {
      return CHIP1A_ERR_TIMEOUT;
    }
    HAL_Delay(1);
  }

  /* DOUT 3바이트. TODO(AINC): CFGR의 AINC 비트 위치가 확인되면 자동 증가
     모드로 한 트랜잭션 버스트 읽기로 교체 (고속 CR에서 tearing 위험 제거).
     현재는 10~80SPS 운용 기준 — 갱신 주기(>=12ms) 대비 3회 읽기(~1ms)라 안전. */
  uint8_t h = 0, m = 0, l = 0;
  int rc = chip1a_read_reg(g, CHIP1_REG_DOUTH, &h);
  if (rc == CHIP1A_OK)
  {
    rc = chip1a_read_reg(g, CHIP1_REG_DOUTM, &m);
  }
  if (rc == CHIP1A_OK)
  {
    rc = chip1a_read_reg(g, CHIP1_REG_DOUTL, &l);
  }
  if (rc != CHIP1A_OK)
  {
    return rc;
  }

  uint32_t raw = ((uint32_t)h << 16) | ((uint32_t)m << 8) | (uint32_t)l;
  if ((raw & 0x800000U) != 0U)
  {
    raw |= 0xFF000000U;   /* sign extend 24 -> 32 */
  }
  *sample = (int32_t)raw;
  return CHIP1A_OK;
}

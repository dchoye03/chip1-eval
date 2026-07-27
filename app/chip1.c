/**
  ******************************************************************************
  * @file    chip1.c
  * @brief   CHIP1 2-wire pulse protocol driver (SPEC.md §4).
  *
  *          Frame anatomy (SCK rising-edge shifted, tSCK min 125ns -> we use
  *          ~1us half periods for margin):
  *            pulse  1..24 : 24-bit sample, MSB first (chip drives SDA)
  *            pulse 25..27 : ST0, ST1, then SDA forced high (always clock
  *                           all 27 so the next DRDY falling edge is clean)
  *          Register access continues the same frame:
  *            pulse 28..29 : config-mode entry
  *            pulse 30..35 : 6-bit register address (host drives SDA)
  *            pulse 36     : direction - SDA high=write, low=read
  *            pulse 37     : turnaround gap
  *            pulse 38..45 : 8-bit data (write: host drives / read: chip drives)
  *            pulse 46     : SDA forced high, frame end
  ******************************************************************************
  */
#include "chip1.h"
#include "board_pins.h"
#include "delay.h"

#define TSCK_US  1U   /* SCK high/low width (min 125ns, generous margin) */

/* Pin helpers ---------------------------------------------------------------*/
static void sck(const chip1_t *g, GPIO_PinState s)
{
  HAL_GPIO_WritePin(g->sck_port, g->sck_pin, s);
}

static void sda_write(const chip1_t *g, uint8_t bit)
{
  HAL_GPIO_WritePin(g->sda_port, g->sda_pin,
                    bit ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t sda_read(const chip1_t *g)
{
  return (HAL_GPIO_ReadPin(g->sda_port, g->sda_pin) == GPIO_PIN_SET) ? 1U : 0U;
}

static void sda_dir(const chip1_t *g, uint32_t mode)
{
  GPIO_InitTypeDef gi = {0};
  gi.Pin   = g->sda_pin;
  gi.Mode  = mode;
  gi.Pull  = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(g->sda_port, &gi);
}

/* One SCK pulse reading SDA on the high phase (data shifts on rising edge) */
static uint8_t pulse_read(const chip1_t *g)
{
  uint8_t bit;

  sck(g, GPIO_PIN_SET);
  delay_us(TSCK_US);
  bit = sda_read(g);
  sck(g, GPIO_PIN_RESET);
  delay_us(TSCK_US);
  return bit;
}

/* One SCK pulse driving SDA (set before the rising edge) */
static void pulse_write(const chip1_t *g, uint8_t bit)
{
  sda_write(g, bit);
  sck(g, GPIO_PIN_SET);
  delay_us(TSCK_US);
  sck(g, GPIO_PIN_RESET);
  delay_us(TSCK_US);
}

/* Wait for conversion done = SDA falling low (DRDY). Reading only after the
   falling edge also keeps us clear of the tUD (~32us) no-clock window. */
static int wait_drdy(const chip1_t *g, uint32_t timeout_ms)
{
  uint32_t start = HAL_GetTick();

  while (sda_read(g) != 0U)
  {
    if ((HAL_GetTick() - start) >= timeout_ms)
    {
      return CHIP1_ERR_TIMEOUT;
    }
  }
  return CHIP1_OK;
}

/* Pulses 1..27: 24-bit sample + ST0/ST1 + SDA released high.
   Precondition: DRDY already seen (SDA low), SDA is input. */
static int32_t data_frame(const chip1_t *g, uint8_t *st0)
{
  uint32_t raw = 0;

  for (int i = 0; i < 24; i++)
  {
    raw = (raw << 1) | pulse_read(g);
  }

  uint8_t s0 = pulse_read(g);   /* pulse 25: ST0 (=1 -> 직전 설정 적용됨) */
  (void)pulse_read(g);          /* pulse 26: ST1 (always 0) */
  (void)pulse_read(g);          /* pulse 27: SDA forced high - always clock it */

  if (st0 != NULL)
  {
    *st0 = s0;
  }

  /* sign-extend 24-bit two's complement */
  if ((raw & 0x800000U) != 0U)
  {
    raw |= 0xFF000000U;
  }
  return (int32_t)raw;
}

/* Pulses 28..46: register access tail. is_write=1 drives val out,
   is_write=0 reads *val from the chip. */
static void reg_tail(const chip1_t *g, uint8_t addr, uint8_t is_write, uint8_t *val)
{
  /* pulses 28..29: config-mode entry (host does not drive) */
  (void)pulse_read(g);
  (void)pulse_read(g);

  /* pulses 30..35: 6-bit address, host drives, MSB first */
  sda_dir(g, GPIO_MODE_OUTPUT_PP);
  for (int i = 5; i >= 0; i--)
  {
    pulse_write(g, (uint8_t)((addr >> i) & 1U));
  }

  /* pulse 36: direction (high=write, low=read) */
  pulse_write(g, is_write);

  if (is_write != 0U)
  {
    /* pulse 37: turnaround gap (keep driving, level don't-care) */
    pulse_write(g, 1U);

    /* pulses 38..45: 8-bit data, host drives */
    for (int i = 7; i >= 0; i--)
    {
      pulse_write(g, (uint8_t)((*val >> i) & 1U));
    }
    sda_dir(g, GPIO_MODE_INPUT);
  }
  else
  {
    /* pulse 37: turnaround gap - release SDA so the chip can drive */
    sda_dir(g, GPIO_MODE_INPUT);
    (void)pulse_read(g);

    /* pulses 38..45: 8-bit data, chip drives */
    uint8_t v = 0;
    for (int i = 0; i < 8; i++)
    {
      v = (uint8_t)((v << 1) | pulse_read(g));
    }
    *val = v;
  }

  /* pulse 46: SDA forced high by the chip, frame end */
  (void)pulse_read(g);
}

/* Public API ----------------------------------------------------------------*/
void chip1_gpio_init(const chip1_t *g)
{
  GPIO_InitTypeDef gi = {0};

  /* SCK LOW before it ever becomes an output - idle high risks power-down */
  HAL_GPIO_WritePin(g->sck_port, g->sck_pin, GPIO_PIN_RESET);

  gi.Pin   = g->sck_pin;
  gi.Mode  = GPIO_MODE_OUTPUT_PP;
  gi.Pull  = GPIO_NOPULL;
  gi.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(g->sck_port, &gi);

  gi.Pin  = g->sda_pin;
  gi.Mode = GPIO_MODE_INPUT;
  HAL_GPIO_Init(g->sda_port, &gi);

  /* NOTE: after DUT power-on (PoR) the interface needs 1.5ms before use;
     after an accidental power-down, wake = SCK low + 350ms (tWU). */
}

int chip1_read_sample(const chip1_t *g, int32_t *sample, uint8_t *st0,
                       uint32_t timeout_ms)
{
  if (wait_drdy(g, timeout_ms) != CHIP1_OK)
  {
    return CHIP1_ERR_TIMEOUT;
  }
  *sample = data_frame(g, st0);
  return CHIP1_OK;
}

int chip1_write_reg(const chip1_t *g, uint8_t addr, uint8_t val,
                     uint32_t timeout_ms)
{
  if (wait_drdy(g, timeout_ms) != CHIP1_OK)
  {
    return CHIP1_ERR_TIMEOUT;
  }
  (void)data_frame(g, NULL);        /* sample read is part of the frame */
  reg_tail(g, addr, 1U, &val);
  return CHIP1_OK;
}

int chip1_read_reg(const chip1_t *g, uint8_t addr, uint8_t *val,
                    uint32_t timeout_ms)
{
  if (wait_drdy(g, timeout_ms) != CHIP1_OK)
  {
    return CHIP1_ERR_TIMEOUT;
  }
  (void)data_frame(g, NULL);
  reg_tail(g, addr, 0U, val);
  return CHIP1_OK;
}

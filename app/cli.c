/**
  ******************************************************************************
  * @file    cli.c
  * @brief   UART CLI: interrupt RX + ring buffer, line parser, dispatch.
  ******************************************************************************
  */
#include "cli.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_main.h"           /* g_adc device instance */
#include "chip1.h"
#include "dac_internal.h"
#include "cal_store.h"
#include "meas.h"

#define ASCII_ESC  0x1B

/* RX ring buffer (filled from the UART IRQ, drained in cli_poll) ------------*/
#define RX_RING_SIZE  256U      /* power of two */

static volatile uint8_t  rx_ring[RX_RING_SIZE];
static volatile uint32_t rx_head = 0;   /* write index (IRQ) */
static volatile uint32_t rx_tail = 0;   /* read index (main loop) */
static uint8_t           rx_byte;       /* HAL 1-byte IT staging */

static UART_HandleTypeDef *cli_uart = NULL;

/* Line editing state --------------------------------------------------------*/
static char     line_buf[CLI_LINE_MAX];
static uint32_t line_len = 0;
static uint8_t  last_was_cr = 0;

/* Output helpers ------------------------------------------------------------*/
void cli_puts(const char *s)
{
  if (cli_uart == NULL)
  {
    return;
  }
  HAL_UART_Transmit(cli_uart, (const uint8_t *)s, (uint16_t)strlen(s), HAL_MAX_DELAY);
}

void cli_println(const char *s)
{
  cli_puts(s);
  cli_puts("\r\n");
}

void cli_printf(const char *fmt, ...)
{
  char buf[128];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  cli_puts(buf);
}

static void cli_prompt(void)
{
  cli_puts("> ");
}

/* RX ring access ------------------------------------------------------------*/
static int rx_pop(void)
{
  if (rx_tail == rx_head)
  {
    return -1;
  }
  uint8_t c = rx_ring[rx_tail & (RX_RING_SIZE - 1U)];
  rx_tail++;
  return (int)c;
}

/* Drain pending input looking for ESC. Used inside long-running 'rd'.
   Non-ESC bytes typed during a capture are discarded. */
static int abort_requested(void)
{
  int c;
  while ((c = rx_pop()) >= 0)
  {
    if (c == ASCII_ESC)
    {
      return 1;
    }
  }
  return 0;
}

/* HAL RX callbacks (weak overrides) -----------------------------------------*/
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == cli_uart)
  {
    /* drop the byte if the ring is full (head-tail distance = size) */
    if ((rx_head - rx_tail) < RX_RING_SIZE)
    {
      rx_ring[rx_head & (RX_RING_SIZE - 1U)] = rx_byte;
      rx_head++;
    }
    HAL_UART_Receive_IT(huart, &rx_byte, 1);   /* re-arm */
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart == cli_uart)
  {
    /* clear overrun/noise/framing and keep receiving */
    __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF |
                                 UART_CLEAR_FEF | UART_CLEAR_PEF);
    HAL_UART_Receive_IT(huart, &rx_byte, 1);
  }
}

/* Argument parsing: decimal by default, 0x prefix = hex (SPEC.md §2) ------*/
static int parse_u32(const char *s, uint32_t *out)
{
  char *end = NULL;
  int base = 10;

  if ((s == NULL) || (*s == '\0'))
  {
    return -1;
  }
  if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X')))
  {
    base = 16;
  }

  unsigned long v = strtoul(s, &end, base);
  if ((end == s) || (*end != '\0'))
  {
    return -1;
  }
  *out = (uint32_t)v;
  return 0;
}

/* Commands ------------------------------------------------------------------*/
static void cmd_help(int argc, char *argv[])
{
  (void)argc; (void)argv;
  cli_println("commands:");
  cli_println("  wr <addr> <val>                  - CHIP1 register write (8bit)");
  cli_println("  rr <addr>                        - CHIP1 register read");
  cli_println("  rd <count>                       - capture samples (ESC aborts)");
  cli_println("  id                               - read chip ID (expect 0x9210)");
  cli_println("  dac init                         - enable DAC ch1/ch2, output 0V");
  cli_println("  dac set <ch> <uV>                - ch=1|2, output voltage (max 3300000)");
  cli_println("  dac cal <ch> <offset_uV> <ppm>   - set calibration coefficients");
  cli_println("  dac cal show                     - show coefficients + per-ch flash state");
  cli_println("  dac cal save [ch]                - persist cal to board flash (ch or both)");
  cli_println("  uid                              - MCU unique ID (board identity)");
  cli_println("  meas                             - measure DVDD/VREF nodes (int. ADC)");
  cli_println("  meas cal                         - ADC self-cal (ADCAL + VREFINT)");
  cli_println("  help                             - this list");
  cli_println("OK");
}

/* 'meas': DVDD/VREF 노드 측정 (16샘플 평균, VREFINT 기반 VDDA 실측 보정)
   'meas cal': ADC 셀프캘 (ADCAL 재실행 + VREFINT 재기준) — 온도 드리프트 대응 */
static void cmd_meas(int argc, char *argv[])
{
  if ((argc == 2) && (strcmp(argv[1], "cal") == 0))
  {
    uint32_t calfactor = 0, vrefint_raw = 0, vdda_mv = 0;
    if (meas_selfcal(&calfactor, &vrefint_raw, &vdda_mv) != MEAS_OK)
    {
      cli_println("ERR meas failed");
      return;
    }
    cli_printf("CALFACTOR=%lu VREFINT_RAW=%lu VDDA=%lu.%03luV\r\n",
               (unsigned long)calfactor, (unsigned long)vrefint_raw,
               (unsigned long)(vdda_mv / 1000U),
               (unsigned long)(vdda_mv % 1000U));
    cli_println("OK");
    return;
  }
  if (argc != 1)
  {
    cli_println("ERR bad arg");
    return;
  }

  uint32_t vdd_mv = 0, vref_mv = 0;
  if (meas_read(&vdd_mv, &vref_mv) != MEAS_OK)
  {
    cli_println("ERR meas failed");
    return;
  }
  cli_printf("VDD=%lu.%03luV VREF=%lu.%03luV\r\n",
             (unsigned long)(vdd_mv / 1000U), (unsigned long)(vdd_mv % 1000U),
             (unsigned long)(vref_mv / 1000U), (unsigned long)(vref_mv % 1000U));
  cli_println("OK");
}

/* signed variant of parse_u32 for calibration coefficients */
static int parse_i32(const char *s, int32_t *out)
{
  uint32_t mag;
  int neg = 0;

  if ((s != NULL) && (*s == '-'))
  {
    neg = 1;
    s++;
  }
  if (parse_u32(s, &mag) != 0 || (mag > 0x7FFFFFFFU))
  {
    return -1;
  }
  *out = neg ? -(int32_t)mag : (int32_t)mag;
  return 0;
}

/* v4: 내장 DAC1 (PA4/PA5) + 지그 보정 계수 (SPEC.md v4 §DAC) */
static void cmd_dac(int argc, char *argv[])
{
  if ((argc == 2) && (strcmp(argv[1], "init") == 0))
  {
    dac_internal_init();
    cli_println("OK");
    return;
  }

  if ((argc == 4) && (strcmp(argv[1], "set") == 0))
  {
    uint32_t ch, uv;
    if ((parse_u32(argv[2], &ch) != 0) || (ch < 1U) || (ch > 2U) ||
        (parse_u32(argv[3], &uv) != 0))
    {
      cli_println("ERR bad arg");
      return;
    }
    if (dac_internal_set_uv((uint8_t)ch, uv) == DAC_INTERNAL_ERR_RANGE)
    {
      cli_println("ERR out of range (max 3300000)");
      return;
    }
    cli_println("OK");
    return;
  }

  if ((argc >= 3) && (strcmp(argv[1], "cal") == 0))
  {
    if ((argc == 3) && (strcmp(argv[2], "show") == 0))
    {
      uint8_t mask = cal_store_mask();
      for (uint8_t ch = 1; ch <= 2; ch++)
      {
        const dac_cal_t *c = dac_internal_get_cal(ch);
        cli_printf("ch%u offset_uV=%ld gain_ppm=%ld\r\n",
                   (unsigned)ch, (long)c->offset_uv, (long)c->gain_ppm);
      }
      cli_printf("flash: ch1=%s ch2=%s\r\n",
                 (mask & 0x1U) ? "saved" : "none",
                 (mask & 0x2U) ? "saved" : "none");
      cli_println("OK");
      return;
    }

    /* 'dac cal save [ch]': 현재 RAM 계수를 보드 플래시에 영구 저장.
       ch 지정 시 그 채널만 갱신(반대 채널 저장분 보존), 생략 시 양 채널. */
    if (((argc == 3) || (argc == 4)) && (strcmp(argv[2], "save") == 0))
    {
      uint8_t mask = 0x3U;
      if (argc == 4)
      {
        uint32_t ch;
        if ((parse_u32(argv[3], &ch) != 0) || (ch < 1U) || (ch > 2U))
        {
          cli_println("ERR bad arg");
          return;
        }
        mask = (ch == 1U) ? 0x1U : 0x2U;
      }
      int32_t off[2], ppm[2];
      const dac_cal_t *c1 = dac_internal_get_cal(1);
      const dac_cal_t *c2 = dac_internal_get_cal(2);
      off[0] = c1->offset_uv; ppm[0] = c1->gain_ppm;
      off[1] = c2->offset_uv; ppm[1] = c2->gain_ppm;
      if (cal_store_save(off, ppm, mask) != CAL_STORE_OK)
      {
        extern uint32_t cal_store_last_err;
        cli_printf("ERR flash save failed (0x%08lX)\r\n",
                   (unsigned long)cal_store_last_err);
        return;
      }
      cli_println("OK");
      return;
    }

    if (argc == 5)
    {
      uint32_t ch;
      int32_t  offset_uv, gain_ppm;
      if ((parse_u32(argv[2], &ch) != 0) || (ch < 1U) || (ch > 2U) ||
          (parse_i32(argv[3], &offset_uv) != 0) ||
          (parse_i32(argv[4], &gain_ppm) != 0))
      {
        cli_println("ERR bad arg");
        return;
      }
      dac_internal_set_cal((uint8_t)ch, offset_uv, gain_ppm);
      /* TODO(cal-reapply): 계수 변경은 다음 'dac set'부터 반영됨. 채널별 마지막
         set 값을 기억해뒀다가 여기서 자동 재출력하는 개선 검토 (2026-07-20 운영
         교훈: cal 후 set 재실행을 잊기 쉬움). */
      cli_println("OK");
      return;
    }

    cli_println("ERR bad arg");
    return;
  }

  cli_println("ERR bad arg");
}

static void cmd_wr(int argc, char *argv[])
{
  uint32_t addr, val;

  if ((argc != 3) ||
      (parse_u32(argv[1], &addr) != 0) || (addr > 0x3FU) ||
      (parse_u32(argv[2], &val) != 0) || (val > 0xFFU))
  {
    cli_println("ERR bad arg");
    return;
  }

  if (chip1_write_reg(&g_adc, (uint8_t)addr, (uint8_t)val,
                       CHIP1_DRDY_TIMEOUT_MS) != CHIP1_OK)
  {
    cli_println("ERR adc no drdy (timeout)");
    return;
  }
  cli_println("OK");
}

static void cmd_rr(int argc, char *argv[])
{
  uint32_t addr;
  uint8_t  val = 0;

  if ((argc != 2) || (parse_u32(argv[1], &addr) != 0) || (addr > 0x3FU))
  {
    cli_println("ERR bad arg");
    return;
  }

  if (chip1_read_reg(&g_adc, (uint8_t)addr, &val,
                      CHIP1_DRDY_TIMEOUT_MS) != CHIP1_OK)
  {
    cli_println("ERR adc no drdy (timeout)");
    return;
  }
  cli_printf("0x%02X\r\n", val);
  cli_println("OK");
}

/* 'uid': MCU 96bit 고유 ID — PC측 보드 식별용 (캘 이력 매칭).
   ⚠ UID(0x08FFF800)는 엔지니어링 바이트 영역 — ICACHE 켠 채 읽으면 HardFault
   (meas.c의 VREFINT 캘값과 동일 함정). 최초 1회만 ICACHE를 끄고 캐시한다. */
static void cmd_uid(int argc, char *argv[])
{
  (void)argc; (void)argv;
  static uint32_t uid[3];
  static uint8_t cached = 0;

  if (!cached)
  {
    HAL_ICACHE_Disable();
    uid[0] = ((const uint32_t *)UID_BASE)[0];
    uid[1] = ((const uint32_t *)UID_BASE)[1];
    uid[2] = ((const uint32_t *)UID_BASE)[2];
    HAL_ICACHE_Enable();
    cached = 1;
  }
  cli_printf("UID=%08lX-%08lX-%08lX\r\n",
             (unsigned long)uid[2], (unsigned long)uid[1],
             (unsigned long)uid[0]);
  cli_println("OK");
}

/* 'id': convenience chip-ID check = wr/rr 배선+전원+프로토콜 동시 검증 (§4 ③) */
static void cmd_id(int argc, char *argv[])
{
  (void)argc; (void)argv;
  uint8_t idh = 0, idl = 0;

  if ((chip1_read_reg(&g_adc, CHIP1_REG_IDH, &idh,
                       CHIP1_DRDY_TIMEOUT_MS) != CHIP1_OK) ||
      (chip1_read_reg(&g_adc, CHIP1_REG_IDL, &idl,
                       CHIP1_DRDY_TIMEOUT_MS) != CHIP1_OK))
  {
    cli_println("ERR adc no drdy (timeout)");
    return;
  }
  cli_printf("0x%02X%02X\r\n", idh, idl);   /* expect 0x9210 */
  cli_println("OK");
}

static void cmd_rd(int argc, char *argv[])
{
  uint32_t count;

  if ((argc != 2) || (parse_u32(argv[1], &count) != 0) || (count == 0U))
  {
    cli_println("ERR bad arg");
    return;
  }

  /* TODO(settling): 채널 전환/입력 급변 후 2~3샘플은 세틀링 무효 (§4.1).
     팀 워크플로우 확인 후 "앞 3샘플 버리기" 옵션 결정. 지금은 기존 펌웨어와
     동일하게 버리지 않음. */

  for (uint32_t i = 0; i < count; i++)
  {
    if (abort_requested())
    {
      cli_println("ERR aborted");
      return;
    }

    int32_t sample;
    if (chip1_read_sample(&g_adc, &sample, NULL,
                           CHIP1_DRDY_TIMEOUT_MS) != CHIP1_OK)
    {
      cli_println("ERR adc no drdy (timeout)");
      return;
    }
    cli_printf("%ld\r\n", (long)sample);   /* stream one line per sample */
  }
  cli_println("OK");
}

/* Dispatch ------------------------------------------------------------------*/
typedef struct
{
  const char *name;
  void (*fn)(int argc, char *argv[]);
} cli_command_t;

static const cli_command_t commands[] = {
  { "help", cmd_help },
  { "wr",   cmd_wr   },
  { "rr",   cmd_rr   },
  { "rd",   cmd_rd   },
  { "id",   cmd_id   },
  { "uid",  cmd_uid  },
  { "dac",  cmd_dac  },
  { "meas", cmd_meas },
};
#define NUM_COMMANDS  (sizeof(commands) / sizeof(commands[0]))

static void cli_execute(char *line)
{
  char *argv[CLI_ARGC_MAX];
  int   argc = 0;

  char *tok = strtok(line, " \t");
  while ((tok != NULL) && (argc < CLI_ARGC_MAX))
  {
    argv[argc++] = tok;
    tok = strtok(NULL, " \t");
  }

  if (argc == 0)
  {
    return;   /* empty line: ignored (§2 출력 규칙) */
  }

  for (uint32_t i = 0; i < NUM_COMMANDS; i++)
  {
    if (strcmp(argv[0], commands[i].name) == 0)
    {
      commands[i].fn(argc, argv);
      return;
    }
  }
  cli_println("ERR unknown command");
}

/* Input handling ------------------------------------------------------------*/
static void cli_handle_char(char c)
{
  /* \r, \n, \r\n 모두 라인 종료로 허용, CRLF의 LF는 무시 */
  if (c == '\n')
  {
    if (last_was_cr)
    {
      last_was_cr = 0;
      return;
    }
    c = '\r';
  }
  last_was_cr = (c == '\r');

  if (c == '\r')
  {
    cli_puts("\r\n");
    line_buf[line_len] = '\0';
    cli_execute(line_buf);
    line_len = 0;
    cli_prompt();
    return;
  }

  if ((c == '\b') || (c == 0x7F))
  {
    if (line_len > 0)
    {
      line_len--;
      cli_puts("\b \b");
    }
    return;
  }

  if ((c < 0x20) || (c > 0x7E))
  {
    return;   /* drop non-printable (incl. stray ESC outside 'rd') */
  }

  if (line_len < (CLI_LINE_MAX - 1))
  {
    line_buf[line_len++] = c;
    HAL_UART_Transmit(cli_uart, (const uint8_t *)&c, 1, HAL_MAX_DELAY);  /* echo */
  }
}

/* Public API ----------------------------------------------------------------*/
void cli_init(UART_HandleTypeDef *huart)
{
  cli_uart = huart;
  line_len = 0;
  rx_head = rx_tail = 0;

  /* interrupt-driven RX into the ring buffer (VCP = USART2 on H533RE) */
  HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
  HAL_UART_Receive_IT(cli_uart, &rx_byte, 1);

  cli_puts("\r\n");
  cli_println("CHIP1 test firmware (NUCLEO-H533RE)");
  cli_prompt();
}

void cli_poll(void)
{
  int c;
  while ((c = rx_pop()) >= 0)
  {
    cli_handle_char((char)c);
  }
}

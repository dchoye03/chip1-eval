/**
  ******************************************************************************
  * @file    cli.h
  * @brief   UART command-line interface (SPEC.md §2).
  *
  *          Contract (Python automation parses this):
  *          - Plain ASCII only. No NUL bytes, no ANSI escapes, no modal menus.
  *          - Line endings: TX always "\r\n"; RX accepts \r, \n, \r\n.
  *          - Every command terminates with "OK" or "ERR <reason>".
  *          - Flat single prompt "> ".
  *          - RX is interrupt-driven into a ring buffer so nothing is lost
  *            while 'rd' streams samples (ESC aborts a running 'rd').
  ******************************************************************************
  */
#ifndef CLI_H
#define CLI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32h5xx_hal.h"

#define CLI_LINE_MAX     96
#define CLI_ARGC_MAX      8

/* Bind to the UART, enable RX interrupt, print banner + prompt. */
void cli_init(UART_HandleTypeDef *huart);

/* Pump: drain the RX ring, echo, execute completed lines. Non-blocking. */
void cli_poll(void);

/* Output helpers (blocking TX). */
void cli_puts(const char *s);
void cli_println(const char *s);
void cli_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* CLI_H */

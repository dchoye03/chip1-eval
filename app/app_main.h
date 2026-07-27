/**
  ******************************************************************************
  * @file    app_main.h
  * @brief   Application entry points + device instances.
  *          main.c stays CubeMX-generated; it only calls app_setup() once
  *          and app_loop() forever.
  ******************************************************************************
  */
#ifndef APP_MAIN_H
#define APP_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "chip1.h"

/* CHIP1 sensor-board instance (pins from board_pins.h, defined in app_main.c) */
extern const chip1_t g_adc;

/* One-time init: delays, driver GPIOs, CLI. Call after BSP COM is up. */
void app_setup(void);

/* One main-loop iteration: heartbeat LED + CLI pump. */
void app_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MAIN_H */

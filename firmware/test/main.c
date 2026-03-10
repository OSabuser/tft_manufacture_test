/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "board.h"

#include "fsl_gpio.h"
#include "pin_mux.h"

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/

int main(void) {
  const uint32_t USER_DELAY_US = 1000000;
  BOARD_Init();
  // GPIO_PinWrite(BOARD_INITPINS_UserLed1_PORT, BOARD_INITPINS_UserLed1_PIN,
  // 0);
  GPIO_PinWrite(BOARD_INITPINS_UserLed2_PORT, BOARD_INITPINS_UserLed2_PIN, 0);
  while (1) {
  }
}

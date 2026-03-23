#pragma once

/**
 * Stub pin_mux.h для host-тестов.
 * Макросы пинов зеркалят реальные значения из generated/pin_mux.h.
 * GPIO-инстансы приходят из fsl_gpio.h (stub_GPIO1/2/3).
 */

#include "fsl_gpio.h"

/* UserLed1 → GPIO3 pin 3 (LED_HEARTBEAT) */
#define BOARD_INITPINS_UserLed1_GPIO            GPIO3
#define BOARD_INITPINS_UserLed1_GPIO_PIN        3U
#define BOARD_INITPINS_UserLed1_INIT_GPIO_VALUE 1U

/* UserLed2 → GPIO3 pin 4 (LED_APP) */
#define BOARD_INITPINS_UserLed2_GPIO            GPIO3
#define BOARD_INITPINS_UserLed2_GPIO_PIN        4U
#define BOARD_INITPINS_UserLed2_INIT_GPIO_VALUE 1U
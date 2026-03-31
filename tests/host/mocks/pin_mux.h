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

/* GPIO_B1_14 (coord C14), TactBut1 */
/* Routed pin properties */
#define BOARD_INITPINS_TactBut1_PERIPHERAL GPIO2   /*!< Peripheral name */
#define BOARD_INITPINS_TactBut1_SIGNAL     gpio_io /*!< Signal name */
#define BOARD_INITPINS_TactBut1_CHANNEL    30U     /*!< Signal channel */

/* Symbols to be used with GPIO driver */
#define BOARD_INITPINS_TactBut1_GPIO                                                               \
    GPIO2                                    /*!< GPIO peripheral base pointer \
                                              */
#define BOARD_INITPINS_TactBut1_GPIO_PIN 30U /*!< GPIO pin number */

/* Symbols to be used with GPIO driver */
#define BOARD_INITPINS_TactBut2_GPIO                                                               \
    GPIO2                                    /*!< GPIO peripheral base pointer \
                                              */
#define BOARD_INITPINS_TactBut2_GPIO_PIN 31U /*!< GPIO pin number */

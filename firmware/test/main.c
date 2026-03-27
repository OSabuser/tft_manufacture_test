
#include "board.h"
#include "bsp/led.h"
#include "bsp/opto.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "log/log.h"
#include "port/log_uart.h"

#include <stdbool.h>
#include <stdint.h>

static volatile bool g_is_in1_activated   = false;
static volatile bool g_is_in2_activated   = false;
static volatile bool g_is_in1_deactivated = false;
static volatile bool g_is_in2_deactivated = false;

static void on_opto_change(bsp_opto_ch_t ch, bsp_opto_state_t state)
{
    if (ch == BSP_OPTO_CH_IN1 && state == BSP_OPTO_STATE_ACTIVE)
    {
        /* IN1 активирован */
        g_is_in1_activated = true;
    }

    if (ch == BSP_OPTO_CH_IN2 && state == BSP_OPTO_STATE_ACTIVE)
    {
        /* IN2 активирован */
        g_is_in2_activated = true;
    }

    if (ch == BSP_OPTO_CH_IN1 && state == BSP_OPTO_STATE_INACTIVE)
    {
        /* IN1 деактивирован */
        g_is_in1_deactivated = true;
    }

    if (ch == BSP_OPTO_CH_IN2 && state == BSP_OPTO_STATE_INACTIVE)
    {
        /* IN2 деактивирован */
        g_is_in2_deactivated = true;
    }
}

int main(void)
{
    const uint16_t DELAY_MS      = 10;
    const uint32_t UART_BAUDRATE = 115200;
    board_hw_init();
    bsp_led_init();
    bsp_tick_init();
    bsp_uart_host_init(UART_BAUDRATE);
    log_uart_init();

    bsp_opto_config_t opto_cfg = {
        .callbacks   = { on_opto_change, on_opto_change, NULL },
        .modes       = { BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL },
        .edges       = { BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING },
        .rs_as_gpio  = false,
        .debounce_ms = DELAY_MS,
    };
    bsp_opto_init(&opto_cfg);
    bsp_led_on(LED_APP);
    LOG_I("BOOT", "firmware_test started, tick=%lu", (unsigned long) bsp_tick_get_ms());

    while (1)
    {
        bsp_opto_process();
        // Контроль включения
        if (g_is_in1_activated)
        {
            g_is_in1_activated = false;
            LOG_D("INPUT", "CH1: ACTIVE!");
        }
        if (g_is_in2_activated)
        {
            g_is_in2_activated = false;
            LOG_D("INPUT", "CH2: ACTIVE!");
        }

        // Контроль выключения
        if (g_is_in1_deactivated)
        {
            g_is_in1_deactivated = false;
            LOG_D("INPUT", "CH1: DISABLED!");
        }
        if (g_is_in2_deactivated)
        {
            g_is_in2_deactivated = false;
            LOG_D("INPUT", "CH2: DISABLED!");
        }

        bsp_delay(DELAY_MS);
    }
}

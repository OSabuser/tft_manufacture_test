
#include "board.h"
#include "bsp/led.h"
#include "bsp/opto.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "log/log.h"
#include "port/log_uart.h"

#include <stdbool.h>
#include <stdint.h>

static volatile bool is_in1_activated = false;
static volatile bool is_in2_activated = false;
static volatile bool is_rs_activated  = false;

static void on_opto_change(bsp_opto_ch_t ch, bsp_opto_state_t state)
{
    if (ch == BSP_OPTO_CH_IN1 && state == BSP_OPTO_STATE_ACTIVE)
    {
        /* IN1 активирован */
        is_in1_activated = true;
    }

    if (ch == BSP_OPTO_CH_IN2 && state == BSP_OPTO_STATE_ACTIVE)
    {
        /* IN2 активирован */
        is_in2_activated = true;
    }

    if (ch == BSP_OPTO_CH_RS && state == BSP_OPTO_STATE_ACTIVE)
    {
        /* RS активирован */
        is_rs_activated = true;
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

    /* --- Opto init --- */
    bsp_opto_config_t opto_cfg = {
        .callbacks   = { on_opto_change, on_opto_change, on_opto_change },
        .edges       = { BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING },
        .rs_as_gpio  = true,
        .debounce_ms = 0, /* instant — для быстрого тестирования */
    };
    bsp_opto_init(&opto_cfg);
    bsp_led_on(LED_APP);
    LOG_I("BOOT", "firmware_test started, tick=%lu", (unsigned long) bsp_tick_get_ms());

    bool in1_enable = false;
    bool in2_enable = false;
    bool rs_enable  = false;
    while (1)
    {
        bsp_opto_process();
        // Контроль включения
        if (is_in1_activated)
        {
            is_in1_activated = false;
            in1_enable       = true;
            LOG_D("INPUT", "CH1: ACTIVE!");
        }
        if (is_in2_activated)
        {
            is_in2_activated = false;
            in2_enable       = true;
            LOG_D("INPUT", "CH2: ACTIVE!");
        }
        if (is_rs_activated)
        {
            is_rs_activated = false;
            rs_enable       = true;
            LOG_D("INPUT", "RS: ACTIVE!");
        }
        // Контроль выключения
        if (in1_enable && bsp_opto_read(BSP_OPTO_CH_IN1) == BSP_OPTO_STATE_INACTIVE)
        {
            in1_enable = false;
            LOG_D("INPUT", "CH1: DISABLED!");
        }
        if (in2_enable && bsp_opto_read(BSP_OPTO_CH_IN2) == BSP_OPTO_STATE_INACTIVE)
        {
            in2_enable = false;
            LOG_D("INPUT", "CH2: DISABLED!");
        }
        if (rs_enable && bsp_opto_read(BSP_OPTO_CH_RS) == BSP_OPTO_STATE_INACTIVE)
        {
            rs_enable = false;
            LOG_D("INPUT", "RS: DISABLED!");
        }
        bsp_delay(DELAY_MS);
    }
}

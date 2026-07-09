/**
 * @file  tests/target/hil_opto/main.c
 * @brief HIL target — CLI для тестирования bsp_opto (оптоизолированные входы).
 *
 * Протокол: текстовые команды через LPUART1 (MCU-Link VCOM), \r\n-terminated.
 *
 * Команды:
 *   PING               -> PONG
 *   OPTO_READ <1|2|3>  -> ACTIVE / INACTIVE
 *   OPTO_EVENTS        -> <число>          (счётчик callback-событий)
 *   OPTO_LAST_EVENT    -> <ch> <state>     (последний канал + состояние)
 *   OPTO_RESET_EVENTS  -> OK               (сбросить счётчики)
 *
 * Стенд:
 *   M5StampPLC: RLY2->RS_RX(ch3), RLY3->EXT_IN1(ch1), RLY4->EXT_IN2(ch2)
 *   (маппинг реле — см. _OPTO_TO_RELAY в tools/hil/m5/agent.py и
 *   docs/testing/hil/HIL_BENCH.md)
 *
 * ВАЖНО: bsp_opto_process() вызывается в каждой итерации main loop.
 *        CLI_RX_TIMEOUT=10ms для быстрого цикла обработки debounce.
 */

#include "board.h"
#include "bsp/led.h"
#include "bsp/opto.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "fsl_gpio.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   128U
#define CLI_RX_TIMEOUT 10U /* мс — короткий, чтобы bsp_opto_process() крутился часто */

/* -------------------------------------------------------------------------- */
/* Event tracking (обновляется из callback, читается из CLI)                  */
/* -------------------------------------------------------------------------- */

static volatile uint32_t s_event_count;
static volatile bsp_opto_ch_t s_last_ch;
static volatile bsp_opto_state_t s_last_state;

static void opto_callback(bsp_opto_ch_t ch, bsp_opto_state_t state)
{
    s_event_count++;
    s_last_ch    = ch;
    s_last_state = state;
}

/* -------------------------------------------------------------------------- */
/* CLI                                                                        */
/* -------------------------------------------------------------------------- */

static size_t cli_read_line(uint8_t *p_buf, size_t max_len)
{
    size_t pos = 0U;

    while (pos < (max_len - 1U))
    {
        int32_t byte = bsp_uart_host_read_byte(CLI_RX_TIMEOUT);
        if (byte < 0)
        {
            break;
        }
        if ((char) byte == '\r')
        {
            continue;
        }
        if ((char) byte == '\n')
        {
            break;
        }
        p_buf[pos++] = (uint8_t) byte;
    }

    p_buf[pos] = '\0';
    return pos;
}

static void cli_process_line(const char *p_line)
{
    char resp[64];

    /* PING */
    if (strncmp(p_line, "PING", 4U) == 0)
    {
        bsp_uart_host_write_str("PONG\r\n");
    }
    /* OPTO_READ <1|2|3> */
    else if (strncmp(p_line, "OPTO_READ ", 10U) == 0)
    {
        unsigned ch_num = 0U;
        if (sscanf(p_line + 10U, "%u", &ch_num) != 1 || ch_num < 1U || ch_num > 3U)
        {
            bsp_uart_host_write_str("ERR_ARG\r\n");
            return;
        }
        /* ch_num 1..3 -> enum 0..2 */
        bsp_opto_state_t st = bsp_opto_read((bsp_opto_ch_t) (ch_num - 1U));
        bsp_uart_host_write_str(st == BSP_OPTO_STATE_ACTIVE ? "ACTIVE\r\n" : "INACTIVE\r\n");
    }
    /* OPTO_EVENTS */
    else if (strncmp(p_line, "OPTO_EVENTS", 11U) == 0)
    {
        snprintf(resp, sizeof(resp), "%lu\r\n", (unsigned long) s_event_count);
        bsp_uart_host_write_str(resp);
    }
    /* OPTO_LAST_EVENT */
    else if (strncmp(p_line, "OPTO_LAST_EVENT", 15U) == 0)
    {
        const char *state_str = (s_last_state == BSP_OPTO_STATE_ACTIVE) ? "ACTIVE" : "INACTIVE";
        snprintf(resp, sizeof(resp), "%u %s\r\n", (unsigned) (s_last_ch + 1U), state_str);
        bsp_uart_host_write_str(resp);
    }
    /* OPTO_RESET_EVENTS */
    else if (strncmp(p_line, "OPTO_RESET_EVENTS", 17U) == 0)
    {
        s_event_count = 0U;
        s_last_ch     = BSP_OPTO_CH_IN1;
        s_last_state  = BSP_OPTO_STATE_INACTIVE;
        bsp_uart_host_write_str("OK\r\n");
    }
    else if (p_line[0] != '\0')
    {
        bsp_uart_host_write_str("ERR_UNKNOWN\r\n");
    }
}

/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);

    /* --- Opto init: все три канала MODE_LEVEL --- */
    bsp_opto_config_t opto_cfg = {
        .callbacks = { opto_callback, opto_callback, opto_callback },
        .modes     = {
            [BSP_OPTO_CH_IN1] = BSP_OPTO_MODE_LEVEL,
            [BSP_OPTO_CH_IN2] = BSP_OPTO_MODE_LEVEL,
            [BSP_OPTO_CH_RS]  = BSP_OPTO_MODE_LEVEL,
        },
        .edges = {
            [BSP_OPTO_CH_IN1] = BSP_OPTO_EDGE_RISING,
            [BSP_OPTO_CH_IN2] = BSP_OPTO_EDGE_RISING,
            [BSP_OPTO_CH_RS]  = BSP_OPTO_EDGE_RISING,
        },
        .rs_as_gpio  = true,
        .debounce_ms = 10U,
    };
    bsp_opto_init(&opto_cfg);

    bsp_led_on(LED_HEARTBEAT);

    /* Шлём READY пока хост не подключится */
    while (bsp_uart_host_rx_available() == 0U)
    {
        bsp_uart_host_write_str("READY\r\n");
        bsp_led_toggle(LED_APP);
        bsp_delay(200U);
    }

    bsp_led_off(LED_APP);
    static uint8_t s_line_buf[CLI_LINE_MAX];

    for (;;)
    {
        bsp_opto_process(); /* <-- ОБЯЗАТЕЛЬНО перед CLI */

        size_t len = cli_read_line(s_line_buf, sizeof(s_line_buf));
        if (len > 0U)
        {
            cli_process_line((const char *) s_line_buf);
        }
    }

    return 0;
}
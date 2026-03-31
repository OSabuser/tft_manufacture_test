#include "board.h"
#include "bsp/button.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"

#include <string.h>

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   64U
#define CLI_RX_TIMEOUT 50U /* мс */
#define POLL_PERIOD_MS 5U  /* интервал debounce-poll */

/* -------------------------------------------------------------------------
 * CLI — чтение строки
 * ---------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------
 * CLI — обработка команды
 *
 * Команды:
 *   PING          → PONG
 *   READ <0|1>    → 1 (нажата) | 0 (не нажата) — сырое чтение
 *   STATE <0|1>   → 1 | 0 — стабильное состояние после debounce
 *   POLL          → OK — один шаг debounce (дополнительно к фоновому)
 *   EVENT_P <0|1> → 1 | 0 — get_event_pressed, сбрасывает флаг
 *   EVENT_R <0|1> → 1 | 0 — get_event_released, сбрасывает флаг
 * ---------------------------------------------------------------------- */

static void cli_process(const char *line)
{
    if (strcmp(line, "PING") == 0)
    {
        bsp_uart_host_write_str("PONG\r\n");
        return;
    }

    if (strcmp(line, "POLL") == 0)
    {
        bsp_button_poll();
        bsp_uart_host_write_str("OK\r\n");
        return;
    }

    /* READ <idx> */
    if (strncmp(line, "READ ", 5U) == 0)
    {
        uint32_t idx = (uint32_t) (line[5] - '0');
        if (idx < (uint32_t) BSP_BUTTON_COUNT)
        {
            bsp_uart_host_write_str(bsp_button_read((bsp_button_t) idx) ? "1\r\n" : "0\r\n");
        }
        else
        {
            bsp_uart_host_write_str("ERR_IDX\r\n");
        }
        return;
    }

    /* STATE <idx> */
    if (strncmp(line, "STATE ", 6U) == 0)
    {
        uint32_t idx = (uint32_t) (line[6] - '0');
        if (idx < (uint32_t) BSP_BUTTON_COUNT)
        {
            bsp_uart_host_write_str(bsp_button_is_pressed((bsp_button_t) idx) ? "1\r\n" : "0\r\n");
        }
        else
        {
            bsp_uart_host_write_str("ERR_IDX\r\n");
        }
        return;
    }

    /* EVENT_P <idx> */
    if (strncmp(line, "EVENT_P ", 8U) == 0)
    {
        uint32_t idx = (uint32_t) (line[8] - '0');
        if (idx < (uint32_t) BSP_BUTTON_COUNT)
        {
            bsp_uart_host_write_str(bsp_button_get_event_pressed((bsp_button_t) idx) ? "1\r\n"
                                                                                     : "0\r\n");
        }
        else
        {
            bsp_uart_host_write_str("ERR_IDX\r\n");
        }
        return;
    }

    /* EVENT_R <idx> */
    if (strncmp(line, "EVENT_R ", 8U) == 0)
    {
        uint32_t idx = (uint32_t) (line[8] - '0');
        if (idx < (uint32_t) BSP_BUTTON_COUNT)
        {
            bsp_uart_host_write_str(bsp_button_get_event_released((bsp_button_t) idx) ? "1\r\n"
                                                                                      : "0\r\n");
        }
        else
        {
            bsp_uart_host_write_str("ERR_IDX\r\n");
        }
        return;
    }

    if (line[0] != '\0')
    {
        bsp_uart_host_write_str("ERR_UNKNOWN\r\n");
    }
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);
    bsp_button_init();

    bsp_led_on(LED_APP);

    /* Шлём READY пока хост не открыл порт */
    while (bsp_uart_host_rx_available() == 0U)
    {
        bsp_uart_host_write_str("READY\r\n");
        bsp_delay(200U);
    }

    uint32_t last_poll_ms = bsp_tick_get_ms();
    static uint8_t s_line[CLI_LINE_MAX];

    for (;;)
    {
        /* Фоновый debounce-poll каждые POLL_PERIOD_MS */
        uint32_t now = bsp_tick_get_ms();
        if ((now - last_poll_ms) >= POLL_PERIOD_MS)
        {
            bsp_button_poll();
            last_poll_ms = now;
        }

        /* Обработка команды если пришла строка */
        size_t len = cli_read_line(s_line, sizeof(s_line));
        if (len > 0U)
        {
            cli_process((const char *) s_line);
        }
    }
}
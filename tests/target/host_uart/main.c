/**
 * @file  tests/target/host_uart/main.c
 * @brief HIL target — UART CLI для тестирования bsp_uart_host.
 *
 * Протокол: текстовые команды через LPUART1 (MCU-Link VCOM), \r\n-terminated.
 *
 * Команды:
 *   PING           → PONG
 *   ECHO <text>    → <text>
 *   UART_BUF_SIZE  → <BSP_UART_HOST_RX_BUFFER_SIZE>
 *
 * Архитектура CLI намеренно простая (strncmp) — EmbeddedCLI подключим
 * когда команд станет >5. Точка расширения: cli_process_line().
 */

#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

#ifndef BSP_UART_HOST_RX_BUFFER_SIZE
#define BSP_UART_HOST_RX_BUFFER_SIZE 256U
#endif

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   128U
#define CLI_RX_TIMEOUT 100U /* мс между байтами при чтении строки */

/* -------------------------------------------------------------------------- */

/**
 * @brief Прочитать строку до \n с таймаутом между байтами.
 * @return Длина строки без \r\n, или 0 при таймауте/пустой строке.
 */
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

/**
 * @brief Диспетчер команд.
 *
 * Точка расширения: заменим тело на EmbeddedCLI когда команд станет > 5.
 * Сигнатура и место вызова из main() не меняются.
 */
static void cli_process_line(const char *p_line)
{
    if (strncmp(p_line, "PING", 4U) == 0)
    {
        bsp_uart_host_write_str("PONG\r\n");
    }
    else if (strncmp(p_line, "ECHO ", 5U) == 0)
    {
        bsp_uart_host_write_str(p_line + 5U);
        bsp_uart_host_write_str("\r\n");
    }
    else if (strncmp(p_line, "UART_BUF_SIZE", 13U) == 0)
    {
        char resp[16];
        snprintf(resp, sizeof(resp), "%u\r\n", (unsigned) BSP_UART_HOST_RX_BUFFER_SIZE);
        bsp_uart_host_write_str(resp);
    }
    else if (p_line[0] != '\0')
    {
        bsp_uart_host_write_str("ERR_UNKNOWN\r\n");
    }
}

/* -------------------------------------------------------------------------- */

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);
    bsp_led_on(LED_HEARTBEAT);

    /* Шлём READY каждые 200 мс пока хост не откроет порт и не пришлёт байт.
     * Как только в RX-буфере что-то появится — переходим в основной цикл.
     * Это убирает race condition между загрузкой ELF и открытием UART. */
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
        size_t len = cli_read_line(s_line_buf, sizeof(s_line_buf));
        if (len > 0U)
        {
            cli_process_line((const char *) s_line_buf);
        }
    }

    return 0;
}
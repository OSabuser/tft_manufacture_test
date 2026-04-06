#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "bsp/usb_cdc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   128U
#define CLI_RX_TIMEOUT 50U

/* ---- UART CLI (управляющий канал через MCU-Link VCOM) ---- */

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
    if (strncmp(p_line, "PING", 4U) == 0)
    {
        bsp_uart_host_write_str("PONG\r\n");
    }
    else if (strncmp(p_line, "USB_READY", 9U) == 0)
    {
        /* Проверка что USB CDC enumeration завершён и хост открыл порт. */
        if (bsp_usb_cdc_is_ready())
        {
            bsp_uart_host_write_str("1\r\n");
        }
        else
        {
            bsp_uart_host_write_str("0\r\n");
        }
    }
    else if (p_line[0] != '\0')
    {
        bsp_uart_host_write_str("ERR_UNKNOWN\r\n");
    }
}

/* ---- USB CDC echo (тестируемый канал) ---- */

static void usb_cdc_echo_process(void)
{
    static uint8_t s_usb_rx_buf[BSP_USB_CDC_MAX_PACKET_SIZE];

    size_t n = bsp_usb_cdc_read(s_usb_rx_buf, sizeof(s_usb_rx_buf));

    if ((n > 0U) && bsp_usb_cdc_write_ready())
    {
        bsp_usb_cdc_write(s_usb_rx_buf, n);
    }
}

/* ---- main ---- */

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);

    /* Инициализация USB CDC — тестируемый модуль. */
    bsp_usb_cdc_init();

    bsp_led_on(LED_HEARTBEAT);

    /* Шлём READY пока хост не открыл UART порт. */
    while (bsp_uart_host_rx_available() == 0U)
    {
        bsp_uart_host_write_str("READY\r\n");
        bsp_delay(200U);
    }

    static uint8_t s_line_buf[CLI_LINE_MAX];

    for (;;)
    {
        /* 1. Обработка UART CLI команд. */
        size_t len = cli_read_line(s_line_buf, sizeof(s_line_buf));

        if (len > 0U)
        {
            cli_process_line((const char *) s_line_buf);
        }

        /* 2. USB CDC echo — всё что пришло по USB, отправляем обратно. */
        usb_cdc_echo_process();

        /* 3. Поллинг USB стека. */
        bsp_usb_cdc_poll();
    }
}
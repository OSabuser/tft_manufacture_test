
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "bsp/usb_cdc.h"
#include "log/log.h"
#include "port/log_uart.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
int main(void)
{
    const uint16_t DELAY_MS      = 100;
    const uint32_t UART_BAUDRATE = 115200;
    board_hw_init();

    bsp_led_init();
    bsp_tick_init();
    bsp_uart_host_init(UART_BAUDRATE);
    log_uart_init();

    LOG_I("BOOT", "firmware_test started, tick=%lu", (unsigned long) bsp_tick_get_ms());

    if (bsp_usb_cdc_init() == BSP_OK)
    {
        LOG_I("BOOT", "USB CDC ACM initialized");
    }

    bsp_led_on(LED_APP);
    bool is_connection_established = false;
    while (1)
    {
        if (bsp_usb_cdc_is_ready() == true && is_connection_established == false)
        {
            is_connection_established = true;
            LOG_I("BOOT", "USB CDC ACM ready");
        }

        if (is_connection_established == true)
        {
            const char *msg = "Hello from TFT Board\r\n";
            bsp_usb_cdc_write((const uint8_t *) msg, strlen(msg));
        }

        bsp_usb_cdc_poll();
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(DELAY_MS);
    }
}

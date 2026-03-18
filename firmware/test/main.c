#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"

int main(void)
{
    const uint16_t DELAY_MS      = 1000;
    const uint32_t UART_BAUDRATE = 115200;
    board_hw_init();
    bsp_led_init();
    bsp_tick_init();
    if (bsp_uart_host_init(UART_BAUDRATE) != BSP_OK)
    {
        while (1)
        {
            bsp_led_toggle(LED_HEARTBEAT);
            bsp_delay(50);
        }
    }

    while (1)
    {
        bsp_led_on(LED_HEARTBEAT);
        bsp_uart_host_write_str("We are here!");
        bsp_delay(DELAY_MS);
        bsp_led_off(LED_HEARTBEAT);
        bsp_delay(DELAY_MS);
    }
}

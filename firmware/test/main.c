
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "log/log.h"
#include "port/log_uart.h"

#include <stdint.h>
int main(void)
{
    const uint16_t DELAY_MS      = 1000;
    const uint32_t UART_BAUDRATE = 115200;
    board_hw_init();
    bsp_led_init();
    bsp_tick_init();
    bsp_uart_host_init(UART_BAUDRATE);
    log_uart_init();

    LOG_I("BOOT", "firmware_test started, tick=%lu", (unsigned long) bsp_tick_get_ms());
    LOG_D("BOOT", "RX buffer ready, waiting for host...");

    uint32_t cycle = 0;
    while (1)
    {
        bsp_led_on(LED_HEARTBEAT);
        LOG_D("CLI", "Running in the loop: '%d'", cycle % UINT32_MAX);
        bsp_delay(DELAY_MS);
        bsp_led_off(LED_HEARTBEAT);
        bsp_delay(DELAY_MS);
    }
}

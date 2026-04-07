
/**
 * @file  main.c
 * @brief firmware_test — точка входа.
 *
 * Bare-metal входной контроль платы.
 * Единственный канал хост↔таргет: USB CDC ACM (J2).
 * Протокол: JSON-lines через cli.c.
 *
 * Последовательность старта:
 *   1. board_hw_init()    — тактирование, MPU, кэш, пины
 *   2. bsp_tick_init()    — SysTick 1 мс
 *   3. bsp_led_init()     — оба LED выключены
 *   4. bsp_usb_cdc_init() — PHY + стек + NVIC
 *   5. Ожидание CDC ready — LED_HEARTBEAT мигает
 *   6. cli_init()         — сброс буфера
 *   7. READY → хост       — JSON сигнал готовности
 *   8. Главный цикл       — poll + cli_process
 */
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "cli.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    const uint32_t CONNECT_BLINK_MS = 200U;
    const uint32_t ERROR_BLINK_MS   = 50;
    board_hw_init();

    bsp_led_init();
    bsp_tick_init();

    if (bsp_usb_cdc_init() != BSP_OK)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(ERROR_BLINK_MS);
    }

    /* Ожидать подключения хоста. LED_HEARTBEAT мигает — прошивка жива. */
    while (!bsp_usb_cdc_is_ready())
    {
        bsp_usb_cdc_poll();
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(CONNECT_BLINK_MS);
    }

    bsp_led_on(LED_APP);

    cli_init();
    cli_send("{\"ok\":true,\"result\":\"READY\"}\n");
    while (1)
    {
        bsp_usb_cdc_poll();
        cli_process();
    }
}

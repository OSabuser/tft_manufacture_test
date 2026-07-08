/**
 * @file  main.c
 * @brief bootloader — точка входа.
 *
 * Фаза 1 (скелет): bring-up + USB CDC ACM + ping/get_version. Без доступа
 * к Flash-слотам, без bootutil, без SD — это добавится в Фазах 2-3.
 *
 * Единственный канал хост↔плата: USB CDC ACM. Протокол: JSON-lines через
 * cli.c (урезанное подмножество протокола firmware_test).
 *
 * Последовательность старта (зеркалит firmware/test/src/main.c):
 *   1. board_hw_init()    — тактирование, MPU, кэш, пины
 *   2. bsp_led_init()     — оба LED выключены
 *   3. bsp_tick_init()    — SysTick 1 мс
 *   4. bsp_usb_cdc_init() — PHY + стек + NVIC
 *   5. Ожидание CDC ready — LED_HEARTBEAT мигает
 *   6. cli_init()         — сброс буфера
 *   7. Главный цикл       — poll + cli_process
 *
 * Bootloader без SDRAM (см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md) — DCD
 * не используется (bsp_boot_xip_no_dcd).
 */
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "cli.h"

#include <stdint.h>

int main(void)
{
    const uint32_t CONNECT_BLINK_MS = 200U;
    const uint32_t ERROR_BLINK_MS   = 250U;

    board_hw_init();

    bsp_led_init();
    bsp_tick_init();

    if (bsp_usb_cdc_init() != BSP_OK)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(ERROR_BLINK_MS);
    }

    /* Ожидать подключения хоста. LED_HEARTBEAT мигает — bootloader жив. */
    while (!bsp_usb_cdc_is_ready())
    {
        bsp_usb_cdc_poll();
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(CONNECT_BLINK_MS);
    }

    bsp_led_on(LED_APP);

    cli_init();
    while (1)
    {
        bsp_usb_cdc_poll();
        cli_process();
    }
}

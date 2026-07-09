/**
 * @file  main.c
 * @brief bootloader — точка входа.
 *
 * Фаза 2: минимальный bring-up → сразу попытка boot_go() (bootutil,
 * Direct-XIP) — так плата грузится в tft_app и без подключённого USB
 * (нормальный полевой сценарий). Только если валидного образа нет ни в
 * одном слоте — поднимаем USB CDC ACM для диагностики (JSON-lines, cli.c,
 * урезанное подмножество протокола firmware_test) и остаёмся в ping/pong
 * цикле. Это прообраз будущего состояния "жду SD" из Фазы 3 — сама
 * SD-логика ещё не добавлена.
 *
 * Последовательность старта:
 *   1. board_hw_init()          — тактирование, MPU, кэш, пины
 *   2. bsp_led_init()           — оба LED выключены
 *   3. bsp_tick_init()          — SysTick 1 мс
 *   4. bsp_qspi_init()          — доступ к Slot A/Б
 *   5. boot_select_and_jump()   — при успехе не возвращается
 *   6. bsp_usb_cdc_init()       — только если п.5 не сработал
 *   7. Ожидание CDC ready       — LED_HEARTBEAT мигает
 *   8. cli_init()               — сброс буфера
 *   9. Главный цикл             — poll + cli_process
 *
 * Bootloader без SDRAM (см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md) — DCD
 * не используется (bsp_boot_xip_no_dcd).
 */
#include "board.h"
#include "boot_select.h"
#include "bsp/led.h"
#include "bsp/qspi_flash.h"
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

    if (bsp_qspi_init() == BSP_OK)
    {
        boot_select_and_jump(); /* при успехе не возвращается */
    }

    /* Нет валидного образа ни в одном слоте — диагностический режим. */
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

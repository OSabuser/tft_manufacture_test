/**
 * @file  main.c
 * @brief Заглушка tft_app для аппаратной верификации Фазы 2 bootutil.
 *
 * tft_app ещё не реализована — bootloader'у некуда прыгать. Этот образ —
 * минимальный, но настоящий imgtool-подписанный XIP-образ с корректным
 * vector table по адресу слота: единственная задача — мигать LED_APP с
 * периодом, зависящим от STUB_BLINK_MS (задаётся компилятору), чтобы по
 * частоте мигания визуально отличить, какой слот реально выбрал
 * bootloader. См. firmware/bootloader/PLAN.md, "Аппаратная верификация
 * Фазы 2".
 *
 * Без USB/CDC — визуальной индикации достаточно, минимальный код.
 *
 * Watchdog: загрузчик взводит аппаратный WDOG перед прыжком сюда, а WDE —
 * write-once (выключить нельзя). Поэтому заглушка ОБЯЗАНА его кормить, иначе
 * WDOG сбросит плату через таймаут и получится reset-loop — заглушка тут
 * играет роль tft_app, которая в проде тоже будет кормить watchdog
 * (см. bsp/wdog/README.md). Период мигания (250/500 мс) << таймаута (~10 c),
 * так что refresh на каждой итерации — с огромным запасом.
 */
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/wdog.h"

#ifndef STUB_BLINK_MS
#error "STUB_BLINK_MS must be defined (see firmware/bootloader/test_stub/CMakeLists.txt)"
#endif

int main(void)
{
    board_hw_init();
    bsp_led_init();
    bsp_tick_init();

    while (1)
    {
        bsp_wdog_refresh(); /* обслуживаем унаследованный от загрузчика WDOG */
        bsp_led_toggle(LED_APP);
        bsp_delay(STUB_BLINK_MS);
    }
}

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
 */
#include "board.h"
#include "bsp/led.h"
#include "bsp/tick.h"

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
        bsp_led_toggle(LED_APP);
        bsp_delay(STUB_BLINK_MS);
    }
}


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
#include "bsp/sd.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "cli.h"
#include "ff.h"
#include "protocol.h"
#include "test_runner.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    const uint32_t CONNECT_BLINK_MS = 200U;
    const uint32_t ERROR_BLINK_MS   = 250;
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

#if 0
    bsp_led_on(LED_APP);

    cli_init();
    test_runner_init();
    protocol_send_session_start();
    while (1)
    {
        bsp_usb_cdc_poll();
        cli_process();
        test_runner_process();
    }
#endif
    static FATFS s_fs;
    const char *step = "card_detect";
    bool passed      = false;

    if (!bsp_sd_is_inserted())
    {
        /* нет карты — пропускаем, это не ошибка стека */
        bsp_usb_cdc_write((const uint8_t *) "The card is not inserted.\r\n", 27);
        goto sd_smoke_done;
    }

    step = "sd_init";
    if (bsp_sd_init() != BSP_OK)
    {
        goto sd_smoke_fail;
    }

    step        = "f_mount";
    FRESULT res = f_mount(&s_fs, "2:/", 1);
    if (res != FR_OK)
    {
        bsp_sd_deinit();
        goto sd_smoke_fail;
    }

    /* всё ок */
    res = f_unmount("2:/");
    if (res != FR_OK)
    {
        bsp_sd_deinit();
        goto sd_smoke_fail;
    }
    bsp_sd_deinit();
    passed = true;

    bsp_usb_cdc_write((const uint8_t *) "The test passed.\r\n", 18);
    goto sd_smoke_done;

sd_smoke_fail:
{
    /* маленький буфер: step не длиннее 16 символов */
    bsp_usb_cdc_write((const uint8_t *) "The test failed.\r\n", 18);
}
    (void) passed;

sd_smoke_done:
    while (1)
    {
        bsp_usb_cdc_poll();
    }
}
/**
 * @file  main.c
 * @brief bootloader — точка входа.
 *
 * Фаза 3: перед выбором образа (bootutil, Direct-XIP) проверяется microSD —
 * если вставлена, sd_update_check() при необходимости ставит более новый
 * (или, при удержании BSP_BUTTON_1, принудительно более старый) подписанный
 * образ в неактивный слот. boot_select_and_jump() вызывается РОВНО ОДИН РАЗ
 * за попытку — если валидного образа нет, main() возвращается в цикл
 * ожидания, где SD периодически пере-сканируется (см. sd_update.h о том,
 * почему boot_go() нельзя звать без новой попытки установки между вызовами).
 *
 * USB CDC поднимается ДО SD-логики (не дожидаясь подключения хоста —
 * bsp_usb_cdc_write() не блокируется без хоста, см. bsp/usb_cdc/src/usb_cdc.c)
 * — чтобы статусы ("installing" и т.п.) были видны, если технолог уже
 * подключён, в т.ч. на самой первой попытке (чек-лист Фазы 3, сценарий 1).
 *
 * Последовательность старта:
 *   1. board_hw_init()          — тактирование, MPU, кэш, пины
 *   2. bsp_wdog_init()          — аппаратный watchdog как можно раньше (см. ниже)
 *   3. bsp_led_init()           — оба LED выключены
 *   4. bsp_tick_init()          — SysTick 1 мс
 *   5. bsp_button_init()        — для проверки удержания BSP_BUTTON_1
 *   6. bsp_qspi_init()          — доступ к Slot A/Б
 *   7. bsp_usb_cdc_init()       — не блокирует, см. выше
 *   8. sd_update_check()        — no-op быстро, если SD не вставлена
 *   9. boot_select_and_jump()   — при успехе не возвращается
 *  10. Цикл ожидания            — CDC ping/pong + LED + периодический
 *                                 пере-скан SD (шаги 8-9 повторно)
 *
 * Watchdog (bsp_wdog): единственная защита от бесконечных зависаний в
 * блокирующих вызовах SDMMC-стека, не возвращающих управление в наш код
 * (SD_PollingCardInsert / OSA_SemaphoreWait — см. DEBUG_LOG_PHASE3_SD.md).
 * ⚠️ WDE — write-once: после взвода watchdog не выключить, он переживает прыжок,
 * поэтому целевой образ (tft_app / test_stub) ОБЯЗАН его кормить (см. bsp/wdog).
 * Кормим только в точках реального прогресса (верх цикла, циклы стирания/
 * копирования, перед прыжком) — НЕ перед f_mount/SD_Init, иначе watchdog
 * перестаёт защищать именно от них.
 *
 * Bootloader без SDRAM (см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md) — DCD
 * не используется (bsp_boot_xip_no_dcd).
 */
#include "board.h"
#include "boot_select.h"
#include "bsp/button.h"
#include "bsp/led.h"
#include "bsp/qspi_flash.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "bsp/wdog.h"
#include "cli.h"
#include "protocol.h"
#include "sd_update.h"

#include <stdbool.h>
#include <stdint.h>

int main(void)
{
    const uint32_t ERROR_BLINK_MS      = 250U;
    const uint32_t SD_RETRY_PERIOD_MS  = 1500U;
    const uint32_t HEARTBEAT_ON_MS     = 50U;
    const uint32_t HEARTBEAT_PERIOD_MS = 500U;
    /* Таймаут WDOG. С запасом над самым долгим НАКОРМЛЕННЫМ участком: между
     * соседними refresh худший легитимный интервал — одиночное стирание 64 КБ
     * блока (~0.15..2 c по даташиту W25Q) либо цепочка bsp_sd_init+f_mount+пик
     * слотов (~2-2.5 c). 10 c даёт кратный запас; зависание ловится ≤10 c. */
    const uint32_t WDOG_TIMEOUT_S = 10U;

    board_hw_init();

    /* Как можно раньше — до первой же SD-логики, которая может зависнуть. */
    (void) bsp_wdog_init(WDOG_TIMEOUT_S);

    bsp_led_init();
    bsp_tick_init();
    bsp_button_init();

    /* Жест форс. даунгрейда — удержание BSP_BUTTON_1 при подаче питания.
     * Сэмплируем РОВНО ЗДЕСЬ, до медленной SD-инициализации, и защёлкиваем на
     * всю сессию: сама установка читает кнопку глубоко внутри run_update()
     * (после mount + двух крипто-валидаций слотов, секунды спустя), поэтому
     * читать её там — неинтуитивно (см. DEBUG_LOG_PHASE3_SD.md, тайминг кнопки).
     * Значение переиспользуется и первой попыткой, и пере-сканами в цикле. */
    const bool DOWNGRADE_HELD = bsp_button_read(BSP_BUTTON_1);

    bool qspi_ok = (bsp_qspi_init() == BSP_OK);
    bool cdc_ok  = (bsp_usb_cdc_init() == BSP_OK);

    if (qspi_ok)
    {
        sd_update_check(DOWNGRADE_HELD); /* no-op быстро, если SD не вставлена */
        bsp_wdog_refresh(); /* образ унаследует полное окно таймаута */
        boot_select_and_jump(); /* при успехе не возвращается */
    }

    /* Нет валидного образа ни в одном слоте (или сбой QSPI) —
     * диагностический режим. */
    if (!cdc_ok)
    {
        bsp_led_toggle(LED_HEARTBEAT);
        bsp_delay(ERROR_BLINK_MS);
    }

    cli_init();

    /* Если предыдущий сброс — по таймауту watchdog, известим (best-effort:
     * если хост ещё не подключён, сообщение потеряется — состояние всегда
     * доступно по команде "wdog", см. cli.c). */
    if (bsp_wdog_caused_last_reset())
    {
        protocol_send_wdog_status();
    }

    /* Готово немедленно — первая попытка сразу извещает "жду SD", не ждёт
     * SD_RETRY_PERIOD_MS. Дальнейшие попытки уже дросселируются периодом. */
    uint32_t next_sd_retry_ms = bsp_tick_get_ms();

    while (1)
    {
        bsp_wdog_refresh(); /* начало итерации — точка реального прогресса */

        bsp_usb_cdc_poll();
        cli_process();

        uint32_t heartbeat_phase_ms = bsp_tick_get_ms() % HEARTBEAT_PERIOD_MS;
        if (heartbeat_phase_ms < HEARTBEAT_ON_MS)
        {
            bsp_led_on(LED_HEARTBEAT);
        }
        else
        {
            bsp_led_off(LED_HEARTBEAT);
        }

        if (qspi_ok && ((int32_t) (bsp_tick_get_ms() - next_sd_retry_ms) >= 0))
        {
            next_sd_retry_ms = bsp_tick_get_ms() + SD_RETRY_PERIOD_MS;

            protocol_send_status("waiting_for_sd");

            sd_update_check(DOWNGRADE_HELD);
            bsp_wdog_refresh(); /* образ унаследует полное окно таймаута */
            boot_select_and_jump(); /* при успехе не возвращается */
        }
    }
}

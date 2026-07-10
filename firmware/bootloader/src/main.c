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
 *   2. bsp_led_init()           — оба LED выключены
 *   3. bsp_tick_init()          — SysTick 1 мс
 *   4. bsp_button_init()        — для проверки удержания BSP_BUTTON_1
 *   5. bsp_qspi_init()          — доступ к Slot A/Б
 *   6. bsp_usb_cdc_init()       — не блокирует, см. выше
 *   7. sd_update_check()        — no-op быстро, если SD не вставлена
 *   8. boot_select_and_jump()   — при успехе не возвращается
 *   9. Цикл ожидания            — CDC ping/pong + LED + периодический
 *                                 пере-скан SD (шаги 7-8 повторно)
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

    board_hw_init();

    bsp_led_init();
    bsp_tick_init();
    bsp_button_init();

    bool qspi_ok = (bsp_qspi_init() == BSP_OK);
    bool cdc_ok  = (bsp_usb_cdc_init() == BSP_OK);

    if (qspi_ok)
    {
        sd_update_check();      /* no-op быстро, если SD не вставлена */
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

    /* Готово немедленно — первая попытка сразу извещает "жду SD", не ждёт
     * SD_RETRY_PERIOD_MS. Дальнейшие попытки уже дросселируются периодом. */
    uint32_t next_sd_retry_ms = bsp_tick_get_ms();

    while (1)
    {
        bsp_usb_cdc_poll();
        cli_process();

        /* "Загрузчик жив" — короткий импульс (50 мс) + долгая пауза (450 мс),
         * безусловно, вне зависимости от CDC. Специально отличим от ровного
         * 50/50 мигания образа в слоте (LED_APP, 500/250 мс) — иначе на глаз
         * не отличить "жив загрузчик" от "прыгнули в образ". Не blocking —
         * bsp_delay() здесь не используется, иначе не успевали бы poll'ить
         * CDC/cli с достаточной частотой. */
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

            sd_update_check();      /* no-op быстро, если SD не вставлена */
            boot_select_and_jump(); /* при успехе не возвращается */
        }
    }
}

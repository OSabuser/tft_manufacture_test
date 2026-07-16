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
 * Фаза 6 (recovery, см. recovery.h): каждая попытка обёрнута в attempt_boot()
 * — после SD-скана, но перед прыжком, recovery_decide() решает, обычная ли
 * это загрузка, нужно ли стереть подозреваемый в зависании слот (счётчик
 * bsp_boot_attempt_count() дошёл до порога, но есть валидный фолбэк), или
 * входить в recovery (порог без фолбэка, или удержан BSP_BUTTON_2). Класс A
 * таксономии (незавершённая установка) закрывается штатным revert MCUboot
 * без участия этой логики.
 *
 * USB CDC поднимается ДО SD-логики (не дожидаясь подключения хоста —
 * bsp_usb_cdc_write() не блокируется без хоста, см. bsp/usb_cdc/src/usb_cdc.c)
 * — чтобы статусы ("installing" и т.п.) были видны, если технолог уже
 * подключён, в т.ч. на самой первой попытке (чек-лист Фазы 3, сценарий 1).
 *
 * Последовательность старта:
 *   1. board_hw_init()          — тактирование, MPU, кэш, пины
 *   2. bsp_wdog_init()          — аппаратный watchdog как можно раньше (см. ниже)
 *   3. bsp_boot_state_init()    — POR-детект + счётчик попыток (Фаза 6)
 *   4. bsp_led_init()           — оба LED выключены
 *   5. bsp_tick_init()          — SysTick 1 мс
 *   6. bsp_button_init()        — для проверки удержания BSP_BUTTON_1/2
 *   7. bsp_qspi_init()          — доступ к Slot A/Б
 *   8. bsp_usb_cdc_init()       — не блокирует, см. выше
 *   9. attempt_boot()           — SD-скан + recovery-гейт + прыжок; при
 *                                 успехе не возвращается
 *  10. Цикл ожидания            — CDC ping/pong + LED + периодический
 *                                 пере-скан SD (шаг 9 повторно)
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
#include "bsp/boot_state.h"
#include "bsp/button.h"
#include "bsp/led.h"
#include "bsp/qspi_flash.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "bsp/wdog.h"
#include "cli.h"
#include "flash_map.h"
#include "protocol.h"
#include "recovery.h"
#include "sd_update.h"
#include "slot_version.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Одна попытка загрузки: recovery-гейт (Фаза 6) → SD-скан → прыжок ─────
 *
 * Порядок важен: recovery_decide() должна знать, входим ли мы в recovery, ДО
 * SD-скана — от этого зависит, каким gate'ом сканировать SD (обычным строгим
 * или ослабленным, update_policy_decide(recovery_mode), см. update_policy.h).
 * Обратный порядок (сначала SD, потом решение) не дал бы recovery-режиму
 * смысла: строгий gate никогда не поставит образ поверх "активного", даже
 * если тот активный и есть подозреваемый в зависании слот.
 *
 * peek_slot() — та же логика, что private peek_slot() в sd_update.c: не
 * шарим напрямую между модулями (см. update_policy.h), копия минимальна.
 * Здесь — только для recovery_decide(); sd_update_check() независимо
 * повторно пикает слоты внутри себя для update_policy_decide(). */
static update_policy_slot_state_t peek_slot(uint8_t fa_id)
{
    update_policy_slot_state_t state;
    state.valid = slot_version_get(fa_id, &state.version);
    return state;
}

static void jump_now(void)
{
    bsp_wdog_refresh(); /* образ унаследует полное окно таймаута */
    boot_select_and_jump(); /* при успехе не возвращается */
}

/**
 * @return true, если по итогам этой попытки мы (остаёмся) в recovery-режиме
 *         — main() использует это для LED-паттерна/CDC-статуса (Фаза 6b).
 */
static bool attempt_boot(bool downgrade_held, bool recovery_held)
{
    update_policy_slot_state_t slot_a = peek_slot(0U);
    update_policy_slot_state_t slot_b = peek_slot(1U);

    recovery_decision_t decision = recovery_decide(
        bsp_boot_attempt_count(), RECOVERY_DEFAULT_THRESHOLD, &slot_a, &slot_b, recovery_held);

    bool enter_recovery = (decision.action == RECOVERY_ENTER_RECOVERY_MODE);

    bool installed = sd_update_check(downgrade_held, enter_recovery);
    if (installed)
    {
        bsp_boot_attempt_reset(); /* новый образ — новый полный бюджет попыток */
    }

    if (enter_recovery)
    {
        if (installed)
        {
            /* Recovery только что поставил валидный образ в Slot A —
             * прыгаем немедленно, не дожидаясь следующего пере-скана.
             * Инкремент — как и в обычном пути (см. RECOVERY_NORMAL_BOOT
             * ниже): первая попытка прыжка в свежий образ тоже расходует
             * бюджет попыток, симметрично обычной установке. */
            bsp_boot_attempt_inc();
            jump_now();
        }
        /* Кандидата не нашлось/не прошёл гейт — остаёмся в recovery. */
        return true;
    }

    switch (decision.action)
    {
    case RECOVERY_ERASE_ACTIVE_THEN_BOOT_OTHER:
    {
        /* Подозреваемый в зависании слот — стереть, есть подтверждённый
         * фолбэк (recovery_decide() это уже проверила). boot_go() внутри
         * jump_now() сам выберет оставшийся. */
        const struct flash_area *p_fap;
        if (flash_area_open((uint8_t) decision.active_slot, &p_fap) == 0)
        {
            (void) flash_area_erase(p_fap, 0U, p_fap->fa_size);
            flash_area_close(p_fap);
        }
        bsp_boot_attempt_reset(); /* ситуация изменилась — новый полный бюджет */
        jump_now();
        break;
    }
    case RECOVERY_NORMAL_BOOT:
    default:
        /* Инкремент только если действительно ЕСТЬ что пытаться загрузить
         * (слот уже валиден, либо только что установлен этим же вызовом) —
         * иначе на чисто пустой плате без SD счётчик рос бы и на пустом
         * месте, и через threshold попыток (несколько секунд) recovery_decide()
         * ошибочно увела бы в recovery-режим при отсутствии какого-либо
         * реального зависания (регрессия к сценарию 4 Фазы 3 — "оба слота
         * пусты" должен оставаться в обычном ожидании SD бесконечно). */
        if (installed || slot_a.valid || slot_b.valid)
        {
            bsp_boot_attempt_inc(); /* перед попыткой — см. bsp/boot_state.h */
        }
        jump_now();
        break;
    }
    return false;
}

int main(void)
{
    const uint32_t ERROR_BLINK_MS      = 250U;
    const uint32_t SD_RETRY_PERIOD_MS  = 1500U;
    const uint32_t HEARTBEAT_ON_MS     = 50U;
    const uint32_t HEARTBEAT_PERIOD_MS = 500U;
    /* Recovery-паттерн (Фаза 6b): оба LED синхронно, 100 мс вкл/100 мс выкл —
     * чётко отличается от heartbeat (50/450, один LED) и app (500/250,
     * один LED, см. test_stub) визуально, без дополнительной телеметрии. */
    const uint32_t RECOVERY_BLINK_ON_MS     = 100U;
    const uint32_t RECOVERY_BLINK_PERIOD_MS = 200U;
    /* Таймаут WDOG. С запасом над самым долгим НАКОРМЛЕННЫМ участком: между
     * соседними refresh худший легитимный интервал — одиночное стирание 64 КБ
     * блока (~0.15..2 c по даташиту W25Q) либо цепочка bsp_sd_init+f_mount+пик
     * слотов (~2-2.5 c). 10 c даёт кратный запас; зависание ловится ≤10 c. */
    const uint32_t WDOG_TIMEOUT_S = 10U;

    board_hw_init();

    /* Как можно раньше — до первой же SD-логики, которая может зависнуть. */
    (void) bsp_wdog_init(WDOG_TIMEOUT_S);

    /* Сразу после watchdog — сама операция дешёвая (пара регистров SRC), а
     * решение recovery_decide() ниже нужно уже на первой попытке. */
    bsp_boot_state_init();

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

    /* Жест recovery (Фаза 6) — тот же приём, удержание BSP_BUTTON_2. Приоритет
     * над BTN_1 разрешается внутри recovery_decide() (проверяется первым). */
    const bool RECOVERY_HELD = bsp_button_read(BSP_BUTTON_2);

    bool qspi_ok = (bsp_qspi_init() == BSP_OK);
    bool cdc_ok  = (bsp_usb_cdc_init() == BSP_OK);

    /* Отслеживает recovery-состояние между попытками — main-loop использует
     * его для LED-паттерна каждую итерацию, не только на попытках прыжка
     * (attempt_boot() зовётся раз в SD_RETRY_PERIOD_MS, LED должен обновляться
     * значительно чаще). */
    bool in_recovery = false;

    if (qspi_ok)
    {
        in_recovery = attempt_boot(DOWNGRADE_HELD, RECOVERY_HELD);
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

        if (in_recovery)
        {
            bool recovery_led_on = (bsp_tick_get_ms() % RECOVERY_BLINK_PERIOD_MS) < RECOVERY_BLINK_ON_MS;
            bsp_led_set(LED_HEARTBEAT, recovery_led_on);
            bsp_led_set(LED_APP, recovery_led_on);
        }
        else
        {
            uint32_t heartbeat_phase_ms = bsp_tick_get_ms() % HEARTBEAT_PERIOD_MS;
            if (heartbeat_phase_ms < HEARTBEAT_ON_MS)
            {
                bsp_led_on(LED_HEARTBEAT);
            }
            else
            {
                bsp_led_off(LED_HEARTBEAT);
            }
        }

        if (qspi_ok && ((int32_t) (bsp_tick_get_ms() - next_sd_retry_ms) >= 0))
        {
            next_sd_retry_ms = bsp_tick_get_ms() + SD_RETRY_PERIOD_MS;

            protocol_send_status(in_recovery ? "recovery_mode" : "waiting_for_sd");

            in_recovery = attempt_boot(DOWNGRADE_HELD, RECOVERY_HELD);
        }
    }
}

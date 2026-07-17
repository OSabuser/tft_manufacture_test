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
 *   9. qspi_info (Фаза 4)       — идентификация чипа QSPI (JEDEC → имя +
 *                                 ёмкость), не зависит от qspi_ok, только CDC
 *  10. bsp_sdram_configure()+   — smoke-test SDRAM/SEMC (Фаза 4): диагностика,
 *      bsp_sdram_init()           не блокирует, результат только на CDC
 *  11. attempt_boot()           — SD-скан + recovery-гейт + прыжок; при
 *                                 успехе не возвращается
 *  12. Цикл ожидания            — CDC ping/pong + LED + периодический
 *                                 пере-скан SD (шаг 11 повторно)
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
 * Bootloader не зависит от SDRAM (см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md)
 * — DCD не используется (bsp_boot_xip_no_dcd), XIP только из W25Q. SEMC/SDRAM
 * трогаются только диагностически, шагом 9 (bsp_sdram_configure(), см.
 * bsp/sdram/README.md) — bootloader сам эту память ни для чего не использует.
 */
#include "board.h"
#include "boot_select.h"
#include "bsp/boot_state.h"
#include "bsp/button.h"
#include "bsp/led.h"
#include "bsp/qspi_flash.h"
#include "bsp/sdram.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "bsp/wdog.h"
#include "cli.h"
#include "flash_map.h"
#include "led_status.h"
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
    const uint32_t ERROR_BLINK_MS     = 250U;
    const uint32_t SD_RETRY_PERIOD_MS = 1500U;
    /* Таймаут WDOG. С запасом над самым долгим НАКОРМЛЕННЫМ участком: между
     * соседними refresh худший легитимный интервал — одиночное стирание 64 КБ
     * блока (~0.15..2 c по даташиту W25Q) либо цепочка bsp_sd_init+f_mount+пик
     * слотов (~2-2.5 c). 10 c даёт кратный запас; зависание ловится ≤10 c. */
    const uint32_t WDOG_TIMEOUT_S = 10U;
    /* Минимум по docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md §1 — карта
     * (bootloader+Slot A+Slot Б+запас под ФС ассетов) рассчитана на W25Q128
     * (16 МБ) и выше; W25Q64 драйвер технически поддерживает, но для этой
     * платы это неверный BOM, а не "чуть меньше запас". */
    const uint32_t QSPI_MIN_FLASH_SIZE_MB = 16U;

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

    /* Единый признак «плата не годна» (LED_BG_HW_FAULT, см. LED_PATTERNS.md) —
     * накапливается по обоим boot-time чекам ниже (QSPI + SDRAM smoke).
     * Детали, что именно не так, всегда есть по CDC (qspi_info/smoke_status);
     * LED показывает лишь факт неисправности. */
    bool hw_fault = false;

    /* Идентификация QSPI-чипа (Фаза 4) — не зависит от qspi_ok:
     * bsp_qspi_read_jedec_id() отрабатывает и после проваленного
     * bsp_qspi_init() (см. её @note), так что "чип не тот"/"чип не опознан"
     * репортится с деталями, а не просто as "не сработало". */
    {
        bsp_qspi_jedec_t jedec = { 0U, 0U };
        uint32_t qspi_size_mb  = 0U;
        const char *p_qspi_chip = "UNKNOWN";
        uint8_t qspi_cap_byte    = 0U;

        if (bsp_qspi_read_jedec_id(&jedec) == BSP_OK)
        {
            qspi_cap_byte = (uint8_t) (jedec.device_id & 0xFFU);
            p_qspi_chip   = bsp_qspi_decode_chip(qspi_cap_byte, &qspi_size_mb);
        }

        const bool QSPI_PASS = (qspi_size_mb >= QSPI_MIN_FLASH_SIZE_MB);
        hw_fault             = (!qspi_ok) || (!QSPI_PASS); /* чип не отвечает / не тот / мал */
        protocol_set_qspi_info(jedec.manufacturer_id, p_qspi_chip, qspi_cap_byte, qspi_size_mb, QSPI_PASS);
        protocol_send_qspi_info(); /* лучший случай — хост уже слушает; см. protocol.h */
    }

    /* Smoke-test SDRAM/SEMC (Фаза 4) — диагностический, неблокирующий: не
     * влияет на attempt_boot() ниже (bootloader SDRAM ни для чего не
     * использует, см. docstring файла), результат только репортится по CDC.
     * Цель — поймать неисправность SEMC/SDRAM на плате раньше, чем её
     * унаследует tft_app (см. bsp/sdram/README.md). */
    bool sdram_ok = (bsp_sdram_configure() == BSP_OK) && (bsp_sdram_init() == BSP_OK);
    hw_fault      = hw_fault || (!sdram_ok);
    protocol_set_smoke_result(sdram_ok);
    protocol_send_smoke_status(); /* лучший случай — хост уже слушает; см. protocol.h */

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

        /* Фоновый паттерн: recovery > неисправность железа > норма (ждём SD).
         * Паттерн «установка» здесь не участвует — он рисуется изнутри самой
         * (блокирующей) установки, см. led_status_tick_install(). */
        led_bg_t bg = in_recovery ? LED_BG_RECOVERY : (hw_fault ? LED_BG_HW_FAULT : LED_BG_WAITING);
        led_status_draw_background(bg);

        if (qspi_ok && ((int32_t) (bsp_tick_get_ms() - next_sd_retry_ms) >= 0))
        {
            next_sd_retry_ms = bsp_tick_get_ms() + SD_RETRY_PERIOD_MS;

            protocol_send_status(in_recovery ? "recovery_mode" : "waiting_for_sd");

            in_recovery = attempt_boot(DOWNGRADE_HELD, RECOVERY_HELD);
        }
    }
}

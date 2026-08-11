/**
 * @file  task_bringup.c
 * @brief Одноразовое bring-up: лог-мьютекс, UART/лог, QSPI+settings+self-confirm,
 *        SDRAM+gfx+CAN. Затем создаёт steady-state задачи и удаляет себя.
 *
 * Слот образа ПОДТВЕРЖДАЕТ СЕБЯ в рантайме (нет revert на повторной загрузке —
 * anti-brick). Контракт с bootloader и обоснование self-confirm/QSPI-ramfunc/
 * UART-диагностики — см. PLAN.md, Фаза 0.
 *
 * Выделено в отдельную задачу на Фазе 3.2.4 (было частью sul_rx_task) — по
 * итогам HW-верификации: одноразовая init-последовательность архитектурно не
 * то же самое, что вечный CAN-цикл, и разделение делает обе задачи проще
 * контролировать по отдельности.
 */

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bootutil/bootutil_public.h"
#include "bsp/boot_state.h"
#include "bsp/display.h"
#include "bsp/qspi_flash.h"
#include "bsp/sdram.h"
#include "bsp/uart_host.h"
#include "crash_log.h"
#include "domain/sul.h"
#include "domain/sul/transport/can.h"
#include "flash_map.h"
#include "log/log.h"
#include "menu/menu_tree.h"
#include "port/log_uart.h"
#include "services/gfx.h"
#include "services/settings_store.h"
#include "task.h"

#include <stdbool.h>

#define LOG_TAG "bringup"

/* Собственный слот образа. Slot A = 0 (primary), Slot Б = 1 (secondary). */
#ifndef APP_OWN_SLOT_ID
#define APP_OWN_SLOT_ID 0
#endif

/* Панель текущего стенда — Фаза 1 хардкод (Фаза 9: provisioning выбирает из
 * settings, см. ARCH §9 — TFT7/8/10 рантайм-выбор внутри профиля app-big). */
#define PANEL_TYPE BSP_DISPLAY_TFT8

/* ── Диагностика трейлера слота (read-only, безопасно звать многократно).
 * Общая для bringup_task (before/after-confirm) и sul_rx_task (periodic) —
 * прототип в app_tasks.h. ──────────────────────────────────────────────── */
void log_slot_status(const char *p_when)
{
    const struct flash_area *p_fap;
    const int RC_OPEN = flash_area_open((uint8_t) APP_OWN_SLOT_ID, &p_fap);
    if (RC_OPEN != 0)
    {
        LOG_E(LOG_TAG, "%s: flash_area_open(slot%d) rc=%d", p_when, APP_OWN_SLOT_ID, RC_OPEN);
        return;
    }

    struct boot_swap_state st = { 0 };
    const int RC_RD           = boot_read_swap_state(p_fap, &st);
    /* Debug (§3.9): это ТЕХНИКА трейлера MCUboot, а не бизнес-событие. Бизнес-
     * факт «образ подтвердил себя» печатает confirm_self() на Info. Строка
     * повторяется раз в 2 с из sul_rx_task — на Info она забивала бы лог. */
    LOG_D(LOG_TAG, "%s: slot%d magic=%d copy_done=%d image_ok=%d (rd=%d)", p_when, APP_OWN_SLOT_ID,
          st.magic, st.copy_done, st.image_ok, RC_RD);

    flash_area_close(p_fap);
}

/**
 * @brief Подтвердить СОБСТВЕННЫЙ слот (APP_OWN_SLOT_ID).
 *
 * boot_set_next(fap, active=true, confirm=true), НЕ boot_set_confirmed(): та
 * жёстко пишет в FLASH_AREA_IMAGE_PRIMARY (Slot A) независимо от исполняемого
 * слота — для Direct-XIP с двумя слотами это подтвердило бы не тот при
 * исполнении из Slot Б.
 */
static void confirm_self(void)
{
    log_slot_status("before-confirm"); /* ожидаем magic=1(GOOD) image_ok=3(UNSET) */

    const struct flash_area *p_fap;
    const int RC_OPEN = flash_area_open((uint8_t) APP_OWN_SLOT_ID, &p_fap);
    if (RC_OPEN != 0)
    {
        LOG_E(LOG_TAG, "confirm: flash_area_open(slot%d) rc=%d", APP_OWN_SLOT_ID, RC_OPEN);
        return;
    }

    const int RC_SET = boot_set_next(p_fap, true, true);
    LOG_I(LOG_TAG, "confirm: boot_set_next rc=%d", RC_SET);

    flash_area_close(p_fap);

    log_slot_status("after-confirm"); /* ожидаем image_ok=1(SET) */
}

/**
 * @brief SDRAM (SEMC, без DCD) + компоновщик gfx + CAN — перед steady-state задачами.
 *
 * bsp_sdram_configure()+init() — та же документированная в bsp/sdram.h связка
 * для «прошивок без DCD», которую использует и bootloader-smoke-test.
 */
static bool bring_up_display_and_can(void)
{
    if (bsp_sdram_configure() != BSP_OK)
    {
        LOG_E(LOG_TAG, "bsp_sdram_configure FAILED");
        return false;
    }
    if (bsp_sdram_init() != BSP_OK)
    {
        LOG_E(LOG_TAG, "bsp_sdram_init FAILED");
        return false;
    }
    if (gfx_init(PANEL_TYPE) != BSP_OK)
    {
        LOG_E(LOG_TAG, "gfx_init FAILED");
        return false;
    }
    if (sul_transport_can_init() != BSP_OK)
    {
        LOG_E(LOG_TAG, "sul_transport_can_init FAILED");
        return false;
    }

    LOG_I(LOG_TAG, "display+CAN bring-up OK");
    return true;
}

/**
 * @brief Индекс пункта меню «Логи» → рантайм-уровень логгера (§3.9).
 *
 * Таблицей, а не арифметикой: значения `LOG_LEVEL_*` не идут подряд
 * (OFF=0, INFO=3, DEBUG=4), и «index+2» сломалось бы на первом же новом
 * пункте. Неизвестный индекс → «Инфо»: неверная настройка не должна
 * приводить к немому устройству.
 */
void app_log_level_apply(uint8_t setting_index)
{
    static const int K_LEVELS[] = {
        LOG_LEVEL_OFF,   /* 0 — Выкл    */
        LOG_LEVEL_INFO,  /* 1 — Инфо    */
        LOG_LEVEL_DEBUG, /* 2 — Отладка */
    };

    const uint8_t COUNT = (uint8_t) (sizeof(K_LEVELS) / sizeof(K_LEVELS[0]));
    log_set_level((setting_index < COUNT) ? K_LEVELS[setting_index] : LOG_LEVEL_INFO);
}

void bringup_task(void *p_arg)
{
    (void) p_arg;

    /* ДО первого LOG_* где-либо в системе — log_write() берёт мьютекс
     * безусловно (port/log/src/log_mutex.c, strong-override weak-NOP).
     * bringup_task — первая и пока единственная запущенная задача, так что
     * порядок гарантирован конструкцией (main создаёт только её). */
    log_mutex_init();

    (void) bsp_uart_host_init(115200U);
    log_uart_init();

    /* ── ТИХАЯ ЗОНА: ни одного LOG_* до применения уровня из настроек ────────
     *
     * Уровень логов живёт в настройках, настройки — на QSPI. Значит всё, что
     * логируется ДО подъёма QSPI, физически не может быть отфильтровано: до
     * первого log_set_level() действует дефолт «не резать ничего сверх
     * компайл-тайм». Раньше сюда попадали три строки (причина сброса, qspi=,
     * settings:), и они шли в UART даже при выбранном «Выкл» — найдено на
     * стенде.
     *
     * Поэтому порядок такой: сначала МОЛЧА поднять QSPI и загрузить настройки,
     * применить уровень — и только потом напечатать всё то же самое. Состав и
     * порядок строк не изменились, изменился момент печати.
     *
     * Плата за это: crash_log_report_previous() уехал ПОСЛЕ bsp_qspi_init(),
     * хотя раньше стоял до любой способной упасть инициализации. Осознанно:
     * при ОТКАЗЕ QSPI (rc != OK) настройки берутся дефолтные, уровень — «Инфо»,
     * и причина всё равно печатается ниже. Потерять её можно только если
     * bsp_qspi_init() ЗАВИСНЕТ намертво — но такая плата не грузится в
     * принципе, и её диагностика — LED-паттерны загрузчика, не наш UART. */
    const bool QSPI_OK = (bsp_qspi_init() == BSP_OK);

    bsp_status_t settings_rc = BSP_OK;
    if (QSPI_OK)
    {
        settings_rc = settings_store_load();
    }
    else
    {
        settings_store_init_defaults();
    }

    /* Активный протокол + меню-секция "Протокол" — из загруженных/дефолтных
     * настроек (§8), до первого возможного открытия меню в menu_task(). */
    sul_registry_set_active(settings_store_get()->device.protocol_id);
    menu_tree_refresh_protocol_section(settings_store_get_mutable());

    /* Рантайм-уровень логов (§3.9) — с этой строки лог подчиняется настройке. */
    app_log_level_apply(settings_store_get()->device.log_level);

    /* ── Конец тихой зоны: печатаем накопленное, в прежнем порядке ─────────── */
    crash_log_report_previous();
    LOG_I(LOG_TAG, "tft_app boot: qspi=%s", QSPI_OK ? "OK" : "FAIL");
    if (QSPI_OK)
    {
        LOG_I(LOG_TAG, "settings: load rc=%d proto_addr=%u", settings_rc,
              settings_store_get()->user.proto_slice[0]);
    }

    /* «Дошёл до устойчивого состояния» — сбрасывает счётчик попыток загрузки
     * (recovery загрузчика). SRC GPR, без flash. Безусловно, до потенциально
     * рискованного bring-up дисплея/CAN ниже. */
    bsp_boot_health_mark();

    if (QSPI_OK)
    {
        confirm_self();
    }
    else
    {
        LOG_E(LOG_TAG, "qspi_init FAILED — self-confirm skipped, slot will revert");
    }

    g_display_ready = bring_up_display_and_can(); /* sul_rx_task/render_task ждут этого */

    /* render_task — первым: его хэндл нужен sul_rx_task/menu_task для
     * xTaskNotifyGive. Порядок формально некритичен (bringup_task —
     * наивысший приоритет из четырёх и монополизирует CPU до своего
     * удаления — никто из троих не может выполниться раньше, чем все три
     * xTaskCreate() ниже отработают), но документирует зависимость явно. */
    (void) xTaskCreate(render_task, "render", APP_TASK_STACK_WORDS, NULL, APP_PRIORITY_RENDER,
                       &g_render_task_handle);
    (void) xTaskCreate(sul_rx_task, "sul_rx", APP_TASK_STACK_WORDS, NULL, APP_PRIORITY_SUL_RX,
                       NULL);
    (void) xTaskCreate(menu_task, "menu", APP_TASK_STACK_WORDS, NULL, APP_PRIORITY_MENU, NULL);

    vTaskDelete(NULL); /* одноразовая задача — дальше нечего делать */
}

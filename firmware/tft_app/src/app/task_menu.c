/**
 * @file  task_menu.c
 * @brief Модель меню: потребление кнопок (debounce — bsp_button/софт-таймер,
 *        независимо от этой задачи), мгновенные вход/навигация/edit/exit+save.
 *
 * Раскладка — как в OLD_PROJECT_TFT8_UKL (без удержания, короткие нажатия):
 * короткое BUTTON_1 = вход в меню (когда закрыто) / следующий пункт (когда
 * открыто); короткое BUTTON_2 = выбор/действие (когда открыто), намеренный
 * no-op вне меню.
 *
 * НЕ рисует — мутирует `g_menu` и будит render_task (xTaskNotifyGive) на любое
 * изменение состояния. Разделено от render_task на Фазе 3.2.4 (HW-находка):
 * раньше (3.2.1–3.2.3, один framebuffer, без ожиданий) потребление кнопок и
 * рендер жили в одной задаче безвредно — блокировок не было. gfx_present()
 * (double-buffer + PXP) внёс блокирующее ожидание кадра; в объединённой
 * задаче это ожидание попутно блокировало вход в меню (ноль реакции на
 * кнопки — см. PLAN.md). Эталон разделения — OLD_PROJECT_TFT8_UKL:
 * BUTTONS_TASK/menu_task отдельно от REFRESH_TASK/tft_refresh_task.
 */

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp/button.h"
#include "bsp/opto.h"
#include "bsp/wdog.h"
#include "crash_log.h"
#include "domain/sul.h"
#include "heartbeat.h"
#include "log/log.h"
#include "menu/menu.h"
#include "menu/menu_tree.h"
#include "services/settings_store.h"
#include "task.h"
#include "timers.h"

#include <stdbool.h>

#define LOG_TAG "menu"

#define MENU_TICK_MS 5U /* каденция потребления кнопок */

menu_ctx_t g_menu;

/**
 * @brief Кормление watchdog — ЕДИНСТВЕННАЯ точка в прошивке (§3.7).
 *
 * ЗДЕСЬ, а не в задаче, потому что демон программных таймеров нельзя
 * выголодать (высший приоритет в системе). Кормим строго по основанию: свежи
 * ВСЕ наблюдаемые задачи (app/heartbeat.h). Протух чей-то heartbeat —
 * кормление прекращается НАВСЕГДА (решение защёлкивается внутри
 * heartbeat_should_feed(), см. там почему это обязательно), и плату штатно
 * сбрасывает аппаратный watchdog; мы лишь успеваем записать, КТО и ГДЕ залип,
 * чтобы сброс не был немым.
 *
 * Никаких LOG_* в этом контексте: стек демона 1 КБ против 4 КБ у задач,
 * vsnprintf там уже стрелял (см. docs/PERF_AND_HANG_INVESTIGATION.md).
 * crash_log_record_stall() форматирования не делает — только запись полей.
 */
static void supervise_watchdog(void)
{
    hb_stall_t stall;

    if (heartbeat_should_feed(app_now_ms(), &stall))
    {
        bsp_wdog_refresh();
        return;
    }

    crash_log_record_stall(heartbeat_task_name(stall.task), stall.p_where, stall.age_ms);
}

/* Софт-таймер (высший приоритет демона) опрашивает debounce независимо от
 * menu_task/render_task — нажатия не теряются, пока кто-то из них занят.
 * Колбэк короткий, без блокировок. */
void input_poll_cb(TimerHandle_t x_timer)
{
    (void) x_timer;
    bsp_button_poll();
    bsp_opto_process(); /* debounce IN1/IN2 (§3.4) — короткая, как button */
    dispatcher_poll(); /* непрерывный опрос bsp_opto_read(), не колбэк — см. dispatcher.c */

    supervise_watchdog(); /* §3.7 — см. выше */
}

void menu_task(void *p_arg)
{
    (void) p_arg;

    menu_init(&g_menu, menu_tree_items(), menu_tree_count(), settings_store_get_mutable());

    TickType_t last_wake = xTaskGetTickCount();

    for (;;)
    {
        heartbeat_mark(HB_TASK_MENU, app_now_ms()); /* «прошла итерацию», §3.7 */

        bool changed = false;

        /* Порядок и структура — как в OLD_PROJECT_TFT8_UKL menu_task():
         * BUTTON_2 читается и обрабатывается только "внутри", BUTTON_1 —
         * читается всегда (вход ИЛИ навигация в зависимости от open). Оба
         * события дренируются безусловно (один get_event_pressed на кнопку
         * за итерацию) — не копится устаревшее состояние. */
        if (bsp_button_get_event_pressed(BSP_BUTTON_2))
        {
            if (menu_is_open(&g_menu))
            {
                menu_action(&g_menu);

                /* Протокол мог смениться этим действием (T_PROTO) — дёшево
                 * переприменить оба (§8), тот же паттерн, что адрес/CAN-
                 * фильтры в sul_rx_task. Секция T_PROTO_PARAM должна отражать
                 * НОВЫЙ активный протокол уже в этом сеансе меню, не только
                 * после save(). */
                sul_registry_set_active(g_menu.settings->device.protocol_id);
                menu_tree_refresh_protocol_section(g_menu.settings);

                /* Уровень логов (§3.9) — тот же паттерн: эффект сразу в этом
                 * сеансе меню, не только после save(). */
                app_log_level_apply(g_menu.settings->device.log_level);

                /* Обновить ДО settings_store_save() (флеш-запись, не
                 * мгновенная) — иначе sul_rx_task ещё несколько мс видел бы
                 * устаревший g_menu_active=true и держал бы мягкую паузу
                 * дольше нужного. */
                g_menu_active = menu_is_open(&g_menu);

                if (!g_menu_active && g_menu.save_requested)
                {
                    /* Выход: сохранить (если менялось). Адрес подхватит
                     * sul_rx_task из настроек на следующей итерации.
                     *
                     * Защищённая операция (§3.7): стирание+запись сектора QSPI
                     * на порядки дольше 5-мс каденции этой задачи — без скобок
                     * супервизор счёл бы её протухшей на каждом сохранении. */
                    heartbeat_enter(HB_TASK_MENU, app_now_ms(), "menu:settings-save");
                    const bsp_status_t RC = settings_store_save();
                    heartbeat_leave(HB_TASK_MENU, app_now_ms());
                    LOG_I(LOG_TAG, "settings saved rc=%d", RC);
                }
                changed = true;
            }
            /* иначе — намеренный no-op вне меню (как в референсе) */
        }
        else if (bsp_button_get_event_pressed(BSP_BUTTON_1))
        {
            if (menu_is_open(&g_menu))
            {
                menu_next(&g_menu);
            }
            else
            {
                menu_open(&g_menu); /* мгновенный вход, без удержания */
            }
            changed = true;
        }

        /* Финальная синхронизация флага мягкой паузы с моделью — покрывает
         * вход/навигацию (выход уже обновил его выше, до save()). */
        g_menu_active = menu_is_open(&g_menu);

        if (changed)
        {
            /* Debug (§3.9): что видит оператор на экране меню — какой пункт
             * текущий и открыто ли оно. Только по СОБЫТИЮ кнопки, не каждую
             * итерацию: цикл здесь 5 мс, безусловный лог был бы флудом. */
            LOG_D(LOG_TAG, "nav: open=%d item=%u", menu_is_open(&g_menu) ? 1 : 0,
                  (unsigned) menu_current(&g_menu));

            if (g_render_task_handle != NULL)
            {
                (void) xTaskNotifyGive(g_render_task_handle);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(MENU_TICK_MS));
    }
}

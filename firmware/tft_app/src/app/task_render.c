/**
 * @file  task_render.c
 * @brief Презентация: единственный владелец дисплея и вызывающий gfx_present*().
 *
 * Event-driven (xTaskNotifyGive от sul_rx_task, menu_task И dispatcher-колбэка
 * opto §3.4 — MPSC, три продюсера, ulTaskNotifyTake(pdTRUE,...) схлопывает
 * несколько notify в одно пробуждение — важно только «есть свежее состояние»,
 * не сколько раз оно менялось). НЕ содержит кнопочной логики — разделено от
 * menu_task на Фазе 3.2.4 (см. task_menu.c про причину).
 *
 * Меню — оконный рендер (Фаза 3.2.4, ускорение навигации): на ОТКРЫТИИ — полная
 * очистка AS (стереть индикацию) + два полных gfx_present() подряд (double
 * buffering: оба FB обязаны получить корректный кадр вне окна — контракт
 * gfx_present_rect); дальше НАВИГАЦИЯ — перерисовка и композит только окна
 * 480×272 (~27% кадра, пропорционально дешевле). Индикация — полные кадры,
 * как и была.
 *
 * Диспетчерский вход (§3.4) — как в OLD_PROJECT_TFT8_UKL: пока меню открыто,
 * индикация вообще не трогается (dispatcher копится в g_dispatcher_indication,
 * применяется одним махом при закрытии — тем же путём, что уже восстанавливает
 * индикацию после меню). Вне меню — САМОСТОЯТЕЛЬНАЯ третья причина перерисовки
 * (не только новое сообщение в очереди/закрытие меню): opto может разбудить
 * render_task без единого нового кадра СУЛ.
 */

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "crash_log.h"
#include "domain/elevator_model.h"
#include "log/log.h"
#include "queue.h"
#include "services/gfx.h"
#include "task.h"
#include "ui/fallback.h"
#include "ui/menu_view.h"

#include <stdbool.h>

#define LOG_TAG "render"

#define DISPLAY_WAIT_POLL_MS 5U /* пока g_display_ready не выставлен bringup_task'ом */

void render_task(void *p_arg)
{
    (void) p_arg;

    while (!g_display_ready)
    {
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_WAIT_POLL_MS));
    }

    sul_result_t last = sul_default_state();
    ui_fallback_render_initial(&last, g_dispatcher_indication);
    gfx_present();

    bool was_menu_open                      = false;
    dispatcher_indication_t last_dispatcher = g_dispatcher_indication;

    for (;;)
    {
        (void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        const bool MENU_OPEN_NOW = menu_is_open(&g_menu);

        if (MENU_OPEN_NOW)
        {
            if (!was_menu_open)
            {
                /* Открытие: стереть индикацию из ВСЕГО AS и прогнать полный
                 * кадр в ОБА FB — после этого вне окна оба буфера корректны
                 * (чёрные), и навигация может обновлять только окно. */
                crash_log_set_breadcrumb("render:menu-open");
                gfx_clear();
                menu_view_render(&g_menu);
                gfx_present();
                gfx_present(); /* тот же AS — во второй FB (double buffering) */
                crash_log_set_breadcrumb("idle");
            }
            else
            {
                /* Навигация/правка: только окно (~27% кадра). */
                crash_log_set_breadcrumb("render:menu-nav");

                /* ЗАМЕР ОТЗЫВЧИВОСТИ (временно, до разбора регресса): по
                 * какой фазе уходит время — рисование в AS (uncached SDRAM,
                 * попиксельно), busy-wait PXP или ожидание FRAME_DONE.
                 * Навигация человеко-темповая, флуда не будет. */
                const TickType_t T0 = xTaskGetTickCount();
                menu_view_render(&g_menu);
                const TickType_t T1 = xTaskGetTickCount();
                gfx_present_rect(0U, 0U, MENU_VIEW_WIN_W, MENU_VIEW_WIN_H);
                const TickType_t T2 = xTaskGetTickCount();

                crash_log_set_breadcrumb("idle");

                LOG_I(LOG_TAG, "menu-nav: draw=%u pxp=%u vsync=%u total=%u мс (err=%u)",
                      (unsigned) ((T1 - T0) * portTICK_PERIOD_MS), (unsigned) g_gfx_last_pxp_ms,
                      (unsigned) g_gfx_last_vsync_ms, (unsigned) ((T2 - T0) * portTICK_PERIOD_MS),
                      (unsigned) g_gfx_present_errors);
            }
        }
        else
        {
            bool present_needed = false;

            /* Снимок один раз за итерацию — g_dispatcher_indication пишет
             * колбэк opto из СВОЕГО контекста (демон таймеров, выше по
             * приоритету, чем render_task); без снимка два обращения ниже
             * могли бы увидеть РАЗНЫЕ значения за одну итерацию (тот же
             * класс гонки, что чинили для курсора меню, см. PLAN.md). */
            const dispatcher_indication_t DISPATCHER_NOW = g_dispatcher_indication;

            if (DISPATCHER_NOW != last_dispatcher)
            {
                /* Лог перехода — ЗДЕСЬ, а не в dispatcher_poll(): та работает
                 * в контексте демона таймеров с 1 КБ стека, vsnprintf там
                 * опасен (см. dispatcher.c). Строго по фронтам: две строки на
                 * прямой переход CALL↔ANSWER (гашение старого + появление
                 * нового), одна — на переход в/из NONE. */
                if (last_dispatcher != DISPATCHER_INDICATION_NONE)
                {
                    LOG_I(LOG_TAG, "mode %s disabled", dispatcher_indication_name(last_dispatcher));
                }
                if (DISPATCHER_NOW != DISPATCHER_INDICATION_NONE)
                {
                    LOG_I(LOG_TAG, "mode %s appeared", dispatcher_indication_name(DISPATCHER_NOW));
                }
            }

            if (was_menu_open || (DISPATCHER_NOW != last_dispatcher))
            {
                /* Меню только что закрылось, ИЛИ диспетчерский вход
                 * изменился без нового кадра СУЛ в очереди (свой продюсер,
                 * не sul_rx_task) — восстановить индикацию последним
                 * известным состоянием СУЛ + ТЕКУЩИМ dispatcher немедленно,
                 * не дожидаясь свежего кадра (sul_rx_task мог простаивать
                 * под g_menu_active — очередь пока пуста). */
                ui_fallback_render_initial(&last, DISPATCHER_NOW);
                present_needed = true;
            }

            render_msg_t msg;
            if (xQueueReceive(g_render_queue, &msg, 0) == pdTRUE)
            {
                last = msg.result;
                ui_fallback_render(&msg.task, &msg.result, DISPATCHER_NOW);
                present_needed = true;
            }

            if (present_needed)
            {
                crash_log_set_breadcrumb("render:indication");
                gfx_present(); /* индикация — всегда полный кадр */
                crash_log_set_breadcrumb("idle");
            }

            last_dispatcher = DISPATCHER_NOW;
        }

        was_menu_open = MENU_OPEN_NOW;
    }
}

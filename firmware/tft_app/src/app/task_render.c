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
#include "domain/elevator_model.h"
#include "heartbeat.h"
#include "log/log.h"
#include "queue.h"
#include "services/gfx.h"
#include "services/settings_store.h"
#include "task.h"
#include "ui/boot_screen.h"
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

    /* ── Загрузочный экран (§3.10) ────────────────────────────────────────
     * Показывается вместо первого кадра индикации: пока он на экране, СУЛ
     * уже принимается и декодируется — задержано только отображение. */
    const uint8_t BOOT_SECS   = settings_store_get()->device.boot_screen_secs;
    bool boot_screen_active   = (BOOT_SECS > 0U);
    uint32_t boot_screen_ends = app_now_ms() + ((uint32_t) BOOT_SECS * 1000U);

    heartbeat_enter(HB_TASK_RENDER, app_now_ms(), "render:first-frame");
    if (boot_screen_active)
    {
        gfx_clear();
        ui_boot_screen_render(&g_boot_screen_info);
        gfx_present();
        gfx_present(); /* тот же AS — во второй FB (double buffering) */
    }
    else
    {
        ui_fallback_render_initial(&last, g_dispatcher_indication);
        gfx_present();
    }
    heartbeat_leave(HB_TASK_RENDER, app_now_ms());

    bool was_menu_open                      = false;
    bool force_indication                   = false;
    dispatcher_indication_t last_dispatcher = g_dispatcher_indication;

    for (;;)
    {
        /* Пока показывается загрузочный экран — ждём НЕ вечно, а до конца
         * показа. Выдержка живёт именно здесь, в ожидании, а НЕ в
         * vTaskDelay() внутри heartbeat_enter/leave: у render_task порог
         * супервизора guard_ms = 2000 мс (§3.7), и трёхсекундный показ под
         * скобками уронил бы плату. В простое же event-driven задача свежа
         * по определению. */
        TickType_t wait = portMAX_DELAY;
        if (boot_screen_active)
        {
            const int32_t LEFT_MS = (int32_t) (boot_screen_ends - app_now_ms());
            wait                  = (LEFT_MS > 0) ? pdMS_TO_TICKS((uint32_t) LEFT_MS) : 0U;
        }

        (void) ulTaskNotifyTake(pdTRUE, wait);

        if (boot_screen_active)
        {
            /* Меню важнее экрана: оператор не должен ждать окончания показа. */
            const bool DISMISS =
                ((int32_t) (app_now_ms() - boot_screen_ends) >= 0) || menu_is_open(&g_menu);

            if (!DISMISS)
            {
                /* Свежее состояние СУЛ ПОГЛОЩАЕМ, но не рисуем — чтобы по
                 * окончании показа индикация появилась сразу актуальной, а не
                 * с «--» до следующего кадра. */
                render_msg_t msg;
                if (xQueueReceive(g_render_queue, &msg, 0) == pdTRUE)
                {
                    last = msg.result;
                }
                continue;
            }

            boot_screen_active = false;
            /* Экран снят — индикацию надо нарисовать явно: очередь может быть
             * пуста, а dispatcher не менялся, и обычные условия ниже не
             * сработали бы. Если открылось меню — его ветка отрисует сама. */
            force_indication = true;
        }

        const bool MENU_OPEN_NOW = menu_is_open(&g_menu);

        if (MENU_OPEN_NOW)
        {
            if (!was_menu_open)
            {
                /* Открытие: стереть индикацию из ВСЕГО AS и прогнать полный
                 * кадр в ОБА FB — после этого вне окна оба буфера корректны
                 * (чёрные), и навигация может обновлять только окно. */
                heartbeat_enter(HB_TASK_RENDER, app_now_ms(), "render:menu-open");
                gfx_clear();
                menu_view_render(&g_menu);
                gfx_present();
                gfx_present(); /* тот же AS — во второй FB (double buffering) */
                heartbeat_leave(HB_TASK_RENDER, app_now_ms());
            }
            else
            {
                /* Навигация/правка: только окно (~27% кадра). */
                heartbeat_enter(HB_TASK_RENDER, app_now_ms(), "render:menu-nav");

                /* ЗАМЕР ОТЗЫВЧИВОСТИ (Debug, §3.9): по какой фазе уходит
                 * время — рисование в AS (uncached SDRAM, попиксельно),
                 * busy-wait PXP или ожидание FRAME_DONE. Именно эту
                 * диагностику сносили после разбора кэшируемого XIP; теперь
                 * она штатная и включается в поле пунктом меню «Логи» →
                 * «Отладка», без пересборки. Навигация человеко-темповая,
                 * флуда не будет. */
                const TickType_t T0 = xTaskGetTickCount();
                menu_view_render(&g_menu);
                const TickType_t T1 = xTaskGetTickCount();
                gfx_present_rect(0U, 0U, MENU_VIEW_WIN_W, MENU_VIEW_WIN_H);
                const TickType_t T2 = xTaskGetTickCount();

                heartbeat_leave(HB_TASK_RENDER, app_now_ms());

                LOG_D(LOG_TAG, "menu-nav: draw=%u pxp=%u vsync=%u total=%u мс (err=%u)",
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

            if (was_menu_open || force_indication || (DISPATCHER_NOW != last_dispatcher))
            {
                force_indication = false;
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
                heartbeat_enter(HB_TASK_RENDER, app_now_ms(), "render:indication");
                const TickType_t T0 = xTaskGetTickCount();
                gfx_present(); /* индикация — всегда полный кадр */
                const TickType_t T1 = xTaskGetTickCount();
                heartbeat_leave(HB_TASK_RENDER, app_now_ms());

                /* Debug (§3.9): полный кадр дороже оконного (весь экран против
                 * 480×272) — на Отладке видно, укладывается ли он в бюджет
                 * guard_ms супервизора (§3.7). Частота — по изменениям СУЛ,
                 * не по кадрам развёртки, флуда нет. */
                LOG_D(LOG_TAG, "indication: present=%u мс pxp=%u vsync=%u (err=%u)",
                      (unsigned) ((T1 - T0) * portTICK_PERIOD_MS), (unsigned) g_gfx_last_pxp_ms,
                      (unsigned) g_gfx_last_vsync_ms, (unsigned) g_gfx_present_errors);
            }

            last_dispatcher = DISPATCHER_NOW;
        }

        was_menu_open = MENU_OPEN_NOW;
    }
}

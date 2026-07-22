/**
 * @file  task_render.c
 * @brief Презентация: единственный владелец дисплея и вызывающий gfx_present().
 *
 * Event-driven (xTaskNotifyGive от sul_rx_task И menu_task — MPSC, будят оба
 * продюсера, ulTaskNotifyTake(pdTRUE,...) схлопывает несколько notify в одно
 * пробуждение — важно только «есть свежее состояние», не сколько раз оно
 * менялось). НЕ содержит кнопочной логики — разделено от menu_task на Фазе
 * 3.2.4 (см. task_menu.c про причину).
 *
 * На каждое пробуждение: меню открыто → рисует меню; иначе — если меню ТОЛЬКО
 * ЧТО закрылось, сразу восстанавливает последнее известное состояние индикации
 * (не дожидаясь свежего сообщения — sul_rx_task мог простаивать под
 * g_menu_active), и дренирует g_render_queue, если там свежий diff.
 */

#include "app_tasks.h"

#include "FreeRTOS.h"
#include "domain/elevator_model.h"
#include "queue.h"
#include "services/gfx.h"
#include "task.h"
#include "ui/fallback.h"
#include "ui/menu_view.h"

#include <stdbool.h>

#define DISPLAY_WAIT_POLL_MS 5U /* пока g_display_ready не выставлен bringup_task'ом */

void render_task(void *p_arg)
{
    (void) p_arg;

    while (!g_display_ready)
    {
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_WAIT_POLL_MS));
    }

    sul_result_t last = sul_default_state();
    ui_fallback_render_initial(&last);
    gfx_present();

    bool was_menu_open = false;

    for (;;)
    {
        (void) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        const bool MENU_OPEN_NOW = menu_is_open(&g_menu);
        bool present_needed      = false;

        if (MENU_OPEN_NOW)
        {
            menu_view_render(&g_menu);
            present_needed = true;
        }
        else
        {
            if (was_menu_open)
            {
                /* Меню только что закрылось — восстановить индикацию
                 * последним известным состоянием немедленно, не дожидаясь
                 * свежего сообщения (sul_rx_task мог простаивать под
                 * g_menu_active — очередь пока пуста). */
                ui_fallback_render_initial(&last);
                present_needed = true;
            }

            render_msg_t msg;
            if (xQueueReceive(g_render_queue, &msg, 0) == pdTRUE)
            {
                last = msg.result;
                ui_fallback_render(&msg.task, &msg.result);
                present_needed = true;
            }
        }

        if (present_needed)
        {
            gfx_present(); /* PXP-композит AS+PS → задний FB + свап (tear-free) */
        }

        was_menu_open = MENU_OPEN_NOW;
    }
}

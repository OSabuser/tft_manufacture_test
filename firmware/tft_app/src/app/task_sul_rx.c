/**
 * @file  task_sul_rx.c
 * @brief Приём CAN → decode → controller → уведомление render_task.
 *
 * WDOG/heartbeat/периодический re-log трейлера — БЕЗУСЛОВНО (housekeeping,
 * не CAN-специфика). Сама CAN-работа (decode/controller/очередь) — под
 * `!g_menu_active`: мягкая пауза на время меню (см. app_tasks.h) — задача НЕ
 * suspend'ится, поэтому WDOG остаётся в безопасности по конструкции.
 *
 * Bring-up (QSPI/settings/self-confirm/SDRAM+gfx+CAN) — bringup_task
 * (task_bringup.c); эта задача стартует уже после него, `g_display_ready`
 * решает, делать ли CAN-работу вообще.
 */

#include "app_tasks.h"

#include "FreeRTOS.h"
#include "bsp/led.h"
#include "bsp/wdog.h"
#include "domain/controller.h"
#include "domain/elevator_model.h"
#include "domain/sul.h"
#include "domain/sul/nku_can.h"
#include "domain/sul/transport/can.h"
#include "queue.h"
#include "services/settings_store.h"
#include "task.h"

#include <stdbool.h>

#define HEARTBEAT_PERIOD_MS 500U
#define WDOG_FEED_PERIOD_MS 100U /* кормим чаще периода мигания — таймаут WDOG >= 1 c */
#define STATUS_LOG_PERIOD_MS                                                                       \
    2000U /* периодический re-log трейлера — виден независимо
                                     * от момента подключения терминала */
#define CAN_RX_TIMEOUT_MS 100U /* держит цикл отзывчивым к WDOG/heartbeat-каденции */
#define CONNECTION_TIMEOUT_MS                                                                      \
    3000U /* «пропадание трафика» — см. ARCH, поток данных:
                                     * poll + timeout→default. Порядок величины — как
                                     * в OLD_PROJECT (там ~3 с на отметку потери связи).
                                     * Меряется от last_frame_tick — пауза меню не портит
                                     * логику: если трафик реально стоял, «--» появится
                                     * сразу по возврату из меню; если шёл — SINCE_FRAME_MS
                                     * обнулится первым же принятым кадром. */

void sul_rx_task(void *p_arg)
{
    (void) p_arg;

    nku_can_ctx_t nku_ctx;
    nku_can_init(&nku_ctx);

    controller_ctx_t ctrl_ctx;
    controller_init(&ctrl_ctx);

    const TickType_t FEED_PERIOD = pdMS_TO_TICKS(WDOG_FEED_PERIOD_MS);
    uint32_t elapsed_ms          = 0U;
    uint32_t since_status_ms     = 0U;
    TickType_t last_wake         = xTaskGetTickCount();
    TickType_t last_frame_tick   = xTaskGetTickCount();

    for (;;)
    {
        bsp_wdog_refresh();

        elapsed_ms += WDOG_FEED_PERIOD_MS;
        if (elapsed_ms >= HEARTBEAT_PERIOD_MS)
        {
            elapsed_ms = 0U;
            bsp_led_toggle(LED_APP);
        }

        since_status_ms += WDOG_FEED_PERIOD_MS;
        if (since_status_ms >= STATUS_LOG_PERIOD_MS)
        {
            since_status_ms = 0U;
            log_slot_status("periodic");
        }

        if (g_display_ready && !g_menu_active)
        {
            /* Адрес станции из настроек (proto_slice[0]) — подхватывает правку из
             * меню без межзадачного сигнала (запись/чтение uint8 атомарны).
             * Обе стороны: decode (nku_ctx) И HW RX-фильтры FlexCAN — вторая
             * без переприменения фильтров под реальный адрес станции кадры с
             * адресом != 0 отбрасывались бы на уровне CAN-контроллера, ещё до
             * decode (см. PLAN.md — найдено на реальной станции, адрес 1). */
            const uint8_t NKU_ADDR = settings_store_get()->user.proto_slice[0];
            nku_can_set_address(&nku_ctx, NKU_ADDR);
            (void) sul_transport_can_set_address(NKU_ADDR); /* no-op, если адрес не менялся */

            sul_result_t decoded;
            bool have_update = false;

            sul_frame_t frame;
            if (sul_transport_can_receive(CAN_RX_TIMEOUT_MS, &frame) == BSP_OK)
            {
                const sul_driver_t *p_driver = sul_registry_active();
                if (p_driver->decode(&nku_ctx, &frame, &decoded) == SUL_STATUS_OK)
                {
                    last_frame_tick = xTaskGetTickCount();
                    have_update     = true;
                }
                /* IGNORED/ERR — Фаза 1 их отдельно не обрабатывает, следующая итерация. */
            }

            const uint32_t SINCE_FRAME_MS =
                (uint32_t) (xTaskGetTickCount() - last_frame_tick) * portTICK_PERIOD_MS;
            if (SINCE_FRAME_MS >= CONNECTION_TIMEOUT_MS)
            {
                /* poll + timeout→default (ARCH, поток данных) — controller сам
                 * определит, реальное ли это изменение (не сработает повторно
                 * на каждой итерации после первого перехода в default). */
                decoded     = sul_default_state();
                have_update = true;
            }

            if (have_update)
            {
                const indication_task_t DIFF = controller_process(&ctrl_ctx, &decoded);
                if (DIFF.pos_pending || DIFF.direction_pending || DIFF.mode_pending)
                {
                    const render_msg_t MSG = { .task = DIFF, .result = decoded };
                    (void) xQueueOverwrite(g_render_queue, &MSG);
                    if (g_render_task_handle != NULL)
                    {
                        (void) xTaskNotifyGive(g_render_task_handle);
                    }
                }
            }
        }

        vTaskDelayUntil(&last_wake, FEED_PERIOD);
    }
}

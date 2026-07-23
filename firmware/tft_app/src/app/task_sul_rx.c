/**
 * @file  task_sul_rx.c
 * @brief Приём активного протокола (реестр sul, §8) → decode → controller →
 *        уведомление render_task. Протокол-специфичный транспорт (CAN/демо)
 *        выбирается здесь по id активного драйвера — единственное место,
 *        которое трогает Фаза 8 при добавлении протокола на новом транспорте.
 *
 * WDOG/heartbeat/периодический re-log трейлера — БЕЗУСЛОВНО (housekeeping,
 * не протокол-специфика). Сам приём (decode/controller/очередь) — под
 * `!g_menu_active`: мягкая пауза на время меню (см. app_tasks.h) — задача НЕ
 * suspend'ится, поэтому WDOG остаётся в безопасности по конструкции.
 *
 * Bring-up (QSPI/settings/self-confirm/SDRAM+gfx+CAN) — bringup_task
 * (task_bringup.c); эта задача стартует уже после него, `g_display_ready`
 * решает, делать ли CAN-работу вообще.
 */

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp/led.h"
#include "bsp/wdog.h"
#include "domain/controller.h"
#include "domain/elevator_model.h"
#include "domain/sul.h"
#include "domain/sul/demo.h"
#include "domain/sul/nku_can.h"
#include "domain/sul/transport/can.h"
#include "domain/sul/transport/demo.h"
#include "log/log.h"
#include "queue.h"
#include "services/settings_store.h"
#include "task.h"

#include <stdbool.h>

#define LOG_TAG "sul_rx"

#define HEARTBEAT_PERIOD_MS 500U
#define WDOG_FEED_PERIOD_MS 100U /* кормим чаще периода мигания — таймаут WDOG >= 1 c */
#define STATUS_LOG_PERIOD_MS                                                                       \
    2000U /* периодический re-log трейлера — виден независимо
                                     * от момента подключения терминала */
#define CAN_RX_TIMEOUT_MS 100U /* держит цикл отзывчивым к WDOG/heartbeat-каденции */
/* «Пропадание трафика» — см. ARCH, поток данных: poll + timeout→default.
 * Порог — СВОЙСТВО ПРОТОКОЛА (p_driver->connection_timeout_ms, domain/sul.h),
 * не константа здесь: разные станции шлют раз в ~200 мс или раз в ~1 с, а
 * некоторые — только по изменению состояния (тогда таймаут вообще отключён,
 * SUL_CONNECTION_TIMEOUT_DISABLED) — не пользовательская настройка, задаётся
 * протоколом при регистрации в sul_registry.c. Меряется от last_frame_tick —
 * пауза меню не портит логику: если трафик реально стоял, «--» появится сразу
 * по возврату из меню; если шёл — SINCE_FRAME_MS обнулится первым же кадром. */

void sul_rx_task(void *p_arg)
{
    (void) p_arg;

    /* Ctx каждого зарегистрированного драйвера — постоянно, живёт в
     * sul_registry.c (domain/sul.h, .p_ctx); переключение протокола не
     * пересоздаёт его, только меняет, какой из них активен (§8). */
    sul_registry_init();

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
            const sul_driver_t *p_driver = sul_registry_active();

            /* Транспорт + реаппликация настроек протокола — ЗДЕСЬ (app-слой =
             * wiring, ARCH §4), не в домене/реестре: держит sul_driver_t.p_ctx
             * и sul_registry_* host-тестируемыми без bsp. Новый протокол на
             * новом транспорте (Фаза 8: УИМ/SD7/УЭЛ/УКЛ, в основном UART) —
             * новая ветка здесь; decode/меню/настройки не трогаются.
             *
             * НКУ-CAN: адрес из настроек (proto_slice[0]) — подхватывает
             * правку из меню без межзадачного сигнала (запись/чтение uint8
             * атомарны). Обе стороны: decode-ctx И HW RX-фильтры FlexCAN —
             * вторая без переприменения фильтров под реальный адрес станции
             * кадры с адресом != 0 отбрасывались бы CAN-контроллером ещё до
             * decode (см. PLAN.md — найдено на реальной станции, адрес 1). */
            sul_frame_t frame;
            bsp_status_t rx_rc;
            if (p_driver->id == SUL_PROTOCOL_NKU_CAN)
            {
                const uint8_t NKU_ADDR = settings_store_get()->user.proto_slice[0];
                nku_can_set_address((nku_can_ctx_t *) p_driver->p_ctx, NKU_ADDR);
                (void) sul_transport_can_set_address(NKU_ADDR); /* no-op, если адрес не менялся */
                rx_rc = sul_transport_can_receive(CAN_RX_TIMEOUT_MS, &frame);
            }
            else /* SUL_PROTOCOL_DEMO — без реальной шины, всегда успешно */
            {
                const uint8_t SPEED_IDX = settings_store_get()->user.proto_slice[0];
                demo_set_speed((demo_ctx_t *) p_driver->p_ctx, SPEED_IDX);
                rx_rc = sul_transport_demo_receive(CAN_RX_TIMEOUT_MS, &frame);
            }

            sul_result_t decoded;
            bool have_update = false;

            if (rx_rc == BSP_OK)
            {
                if (p_driver->decode(p_driver->p_ctx, &frame, &decoded) == SUL_STATUS_OK)
                {
                    last_frame_tick = xTaskGetTickCount();
                    have_update     = true;
                }
                /* IGNORED/ERR — Фаза 1 их отдельно не обрабатывает, следующая итерация. */

                /* Протокол сам инициирует запись в свой proto_slice (§8) — напр.
                 * удалённая адресация НКУ-CAN (§3.5). Полностью generic: НЕТ
                 * ветки по id/протоколу — take_pending_write() либо NULL
                 * (протокол никогда этого не делает), либо сообщает готовый
                 * {offset, value}, домен settings не касается вообще. Вызывается
                 * НЕЗАВИСИМО от SUL_STATUS_OK/IGNORED/ERR выше — протокол мог
                 * распознать команду в кадре, который не подошёл ни под одну
                 * "индикационную" классификацию (напр. кадр от станции с чужим
                 * адресом, см. nku_can.c check_remote_address()). */
                if (p_driver->take_pending_write != NULL)
                {
                    sul_slice_write_t write;
                    if (p_driver->take_pending_write(p_driver->p_ctx, &write))
                    {
                        uint8_t *p_field =
                            &settings_store_get_mutable()->user.proto_slice[write.slice_offset];
                        /* Идемпотентность — ОБЩИЙ гейт для любого протокола, не
                         * его забота: без сравнения контроллер станции, держащий
                         * команду записи некоторое время (см. PDF НКУ-CAN п.5),
                         * писал бы флеш на КАЖДЫЙ повторный кадр. */
                        if (*p_field != write.value)
                        {
                            *p_field                   = write.value;
                            const bsp_status_t SAVE_RC = settings_store_save();
                            LOG_I(LOG_TAG, "%s: proto_slice[%u]=%u (rc=%d)", p_driver->p_name,
                                  write.slice_offset, write.value, SAVE_RC);
                        }
                    }
                }
            }

            if (p_driver->connection_timeout_ms != SUL_CONNECTION_TIMEOUT_DISABLED)
            {
                const uint32_t SINCE_FRAME_MS =
                    (uint32_t) (xTaskGetTickCount() - last_frame_tick) * portTICK_PERIOD_MS;
                if (SINCE_FRAME_MS >= p_driver->connection_timeout_ms)
                {
                    /* poll + timeout→default (ARCH, поток данных) — controller
                     * сам определит, реальное ли это изменение (не сработает
                     * повторно на каждой итерации после перехода в default). */
                    decoded     = sul_default_state();
                    have_update = true;
                }
            }
            /* иначе — протокол event-driven на станции (шлёт только по
             * изменению), детекция обрыва по тишине для него некорректна —
             * last_frame_tick тем не менее продолжает обновляться выше на
             * каждый валидный кадр, просто здесь не используется. */

            if (have_update)
            {

                const indication_task_t DIFF = controller_process(&ctrl_ctx, &decoded);
                if (DIFF.pos_pending || DIFF.direction_pending || DIFF.mode_pending)
                {

                    LOG_I(LOG_TAG,
                          "update from %s: pos=%s next=%s dir=%u arrival=%d move=%d overload=%d "
                          "fire=%d lading=%d maint=%d fireman=%d seismic=%d err=%d floor=%u",
                          p_driver->p_name, decoded.pos, decoded.next, (unsigned) decoded.direction,
                          decoded.arrival, decoded.movement, decoded.overload, decoded.fire_alarm,
                          decoded.lading, decoded.maintenance, decoded.fireman, decoded.seismic,
                          decoded.error, decoded.floor_num);

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

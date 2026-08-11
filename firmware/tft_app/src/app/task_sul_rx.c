/**
 * @file  task_sul_rx.c
 * @brief Приём активного протокола (реестр sul, §8) → decode → controller →
 *        уведомление render_task. Протокол-специфичный транспорт (CAN/демо)
 *        выбирается здесь по id активного драйвера — единственное место,
 *        которое трогает Фаза 8 при добавлении протокола на новом транспорте.
 *
 * Heartbeat супервизора (§3.7), LED-heartbeat и периодический re-log трейлера
 * — БЕЗУСЛОВНО (housekeeping, не протокол-специфика). Сам приём
 * (decode/controller/очередь) — под `!g_menu_active`: мягкая пауза на время
 * меню (см. app_tasks.h) — задача НЕ suspend'ится и продолжает крутить цикл,
 * поэтому пауза не выглядит зависанием для супервизора.
 *
 * Bring-up (QSPI/settings/self-confirm/SDRAM+gfx+CAN) — bringup_task
 * (task_bringup.c); эта задача стартует уже после него, `g_display_ready`
 * решает, делать ли CAN-работу вообще.
 */

#include "FreeRTOS.h"
#include "app_tasks.h"
#include "bsp/led.h"
#include "domain/controller.h"
#include "domain/elevator_model.h"
#include "domain/sul.h"
#include "domain/sul/demo.h"
#include "domain/sul/nku_can.h"
#include "domain/sul/transport/can.h"
#include "domain/sul/transport/demo.h"
#include "domain/sul/uim.h"
#include "heartbeat.h"
#include "log/log.h"
#include "queue.h"
#include "services/settings_store.h"
#include "task.h"

#include <stdbool.h>

#define LOG_TAG "sul_rx"

#define LED_HEARTBEAT_PERIOD_MS 500U
/* Каденция итерации цикла — она же каденция heartbeat'а этой задачи (§3.7).
 * Порог свежести для неё задан в app/heartbeat.c (K_TASKS) с запасом ×20 к
 * этому числу: задача с самым низким приоритетом законно задерживается под
 * нагрузкой, ложный сброс хуже позднего. */
#define LOOP_PERIOD_MS 100U
#define STATUS_LOG_PERIOD_MS                                                                       \
    2000U /* периодический re-log трейлера — виден независимо
                                     * от момента подключения терминала */
#define CAN_RX_TIMEOUT_MS 100U /* держит цикл отзывчивым к каденции heartbeat */
/* Отклик протокола (take_pending_tx) — TX-мейлбокс свободен практически всегда;
 * короткий таймаут, чтобы неисправная шина не съедала бюджет итерации. */
#define CAN_TX_TIMEOUT_MS 10U

/* «Пропадание трафика» — см. ARCH, поток данных: poll + timeout→default.
 * Порог — СВОЙСТВО ПРОТОКОЛА (p_driver->connection_timeout_ms, domain/sul.h),
 * не константа здесь: разные станции шлют раз в ~200 мс или раз в ~1 с, а
 * некоторые — только по изменению состояния (тогда таймаут вообще отключён,
 * SUL_CONNECTION_TIMEOUT_DISABLED) — не пользовательская настройка, задаётся
 * протоколом при регистрации в sul_registry.c. Меряется от last_frame_tick —
 * пауза меню не портит логику: если трафик реально стоял, «--» появится сразу
 * по возврату из меню; если шёл — SINCE_FRAME_MS обнулится первым же кадром. */

/**
 * @brief Отправитель кадра активного транспорта — выбирается в той же ветке,
 *        что и приём, используется generic-кодом отклика (take_pending_tx).
 */
typedef bsp_status_t (*sul_tx_send_fn_t)(const sul_tx_frame_t *p_frame, uint32_t timeout_ms);

void sul_rx_task(void *p_arg)
{
    (void) p_arg;

    /* Ctx каждого зарегистрированного драйвера — постоянно, живёт в
     * sul_registry.c (domain/sul.h, .p_ctx); переключение протокола не
     * пересоздаёт его, только меняет, какой из них активен (§8). */
    sul_registry_init();

    controller_ctx_t ctrl_ctx;
    controller_init(&ctrl_ctx);

    const TickType_t LOOP_PERIOD = pdMS_TO_TICKS(LOOP_PERIOD_MS);
    TickType_t last_wake         = xTaskGetTickCount();
    TickType_t last_frame_tick   = xTaskGetTickCount();

    /* Дедлайны по РЕАЛЬНЫМ тикам, а не «+100 мс за итерацию».
     *
     * Раньше счётчики инкрементировались на LOOP_PERIOD_MS каждый проход,
     * т.е. предполагали, что итерация длится ровно 100 мс. Вне меню так и было
     * (busy-spin в bsp_can_receive съедает таймаут), но ПОД МЕНЮ CAN-блок
     * пропускается — итерация становится микросекундной, vTaskDelayUntil() при
     * просроченном дедлайне не блокирует, и «мс» бежали в сотни раз быстрее
     * реального времени: heartbeat частил, а log_slot_status() (это ЧТЕНИЯ
     * QSPI) вызывался вместо раза в 2 с — десятки раз в секунду. */
    TickType_t next_heartbeat = xTaskGetTickCount();
    TickType_t next_status    = xTaskGetTickCount();

    for (;;)
    {
        /* «Прошла итерацию» (§3.7). Кормит watchdog НЕ эта задача, а демон
         * программных таймеров — и только когда свежи ВСЕ наблюдаемые задачи
         * (см. supervise_watchdog() в task_menu.c). Отметка БЕЗУСЛОВНА, до
         * ветки !g_menu_active: мягкая пауза на время меню — не признак
         * зависания, задача продолжает крутить цикл. */
        heartbeat_mark(HB_TASK_SUL_RX, app_now_ms());

        const TickType_t NOW = xTaskGetTickCount();

        /* Знаковая разница — корректна при перевороте счётчика тиков. */
        if ((int32_t) (NOW - next_heartbeat) >= 0)
        {
            next_heartbeat = NOW + pdMS_TO_TICKS(LED_HEARTBEAT_PERIOD_MS);
            bsp_led_toggle(LED_APP);
        }

        if ((int32_t) (NOW - next_status) >= 0)
        {
            next_status = NOW + pdMS_TO_TICKS(STATUS_LOG_PERIOD_MS);
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
            /* Debug (§3.9): реаппликация настроек протокола идёт КАЖДУЮ
             * итерацию (10 раз/с) и почти всегда — no-op. Логируем только
             * ФАКТ изменения, иначе Отладка утонула бы в повторах. static —
             * задача единственная и вечная. */
            static uint8_t s_logged_proto = 0xFFU;
            static uint8_t s_logged_param = 0xFFU;
            const uint8_t PARAM_NOW       = settings_store_get()->user.proto_slice[0];
            if ((p_driver->id != s_logged_proto) || (PARAM_NOW != s_logged_param))
            {
                s_logged_proto = p_driver->id;
                s_logged_param = PARAM_NOW;
                LOG_D(LOG_TAG, "protocol=%s proto_slice[0]=%u timeout=%u мс", p_driver->p_name,
                      (unsigned) PARAM_NOW, (unsigned) p_driver->connection_timeout_ms);
            }

            sul_frame_t frame;
            bsp_status_t rx_rc;
            /* Отправитель ТОГО ЖЕ транспорта, что и приём — выбирается здесь,
             * в единственной protocol-aware ветке, и используется ниже уже
             * generic-кодом (см. take_pending_tx). NULL — у протокола нет
             * исходящего пути (демо: синтетика без шины). */
            sul_tx_send_fn_t p_send = NULL;
            if (p_driver->id == SUL_PROTOCOL_NKU_CAN)
            {
                const uint8_t NKU_ADDR = settings_store_get()->user.proto_slice[0];
                nku_can_set_address((nku_can_ctx_t *) p_driver->p_ctx, NKU_ADDR);
                /* no-op, если раскладка фильтров не менялась */
                (void) sul_transport_can_set_address_nku(NKU_ADDR);
                rx_rc  = sul_transport_can_receive(CAN_RX_TIMEOUT_MS, &frame);
                p_send = sul_transport_can_send;
            }
            else if (p_driver->id == SUL_PROTOCOL_UIM)
            {
                /* Тот же паттерн, что у НКУ-CAN, но своя раскладка фильтров:
                 * у УИМ CAN ID кадра РАВЕН адресу индикатора. Оба конца
                 * (decode-ctx И HW-фильтр) — из одной настройки. */
                const uint8_t UIM_ADDR = settings_store_get()->user.proto_slice[0];
                uim_set_address((uim_ctx_t *) p_driver->p_ctx, UIM_ADDR);
                (void) sul_transport_can_set_address_uim(UIM_ADDR);
                rx_rc  = sul_transport_can_receive(CAN_RX_TIMEOUT_MS, &frame);
                p_send = sul_transport_can_send;
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

                /* Протокол сам инициирует ОТПРАВКУ кадра — напр. обязательный
                 * отклик станции у УИМ-6100. Тот же generic-паттерн, что и
                 * take_pending_write выше: ветки по id здесь НЕТ, протокол лишь
                 * говорит ЧТО отправить, транспорт (p_send, выбран в ветке
                 * приёма) — КУДА и КАК. Домен шину не трогает (ARCH §4). */
                if ((p_driver->take_pending_tx != NULL) && (p_send != NULL))
                {
                    sul_tx_frame_t tx;
                    if (p_driver->take_pending_tx(p_driver->p_ctx, &tx))
                    {
                        const bsp_status_t TX_RC = p_send(&tx, CAN_TX_TIMEOUT_MS);
                        if (TX_RC != BSP_OK)
                        {
                            LOG_W(LOG_TAG, "%s: tx id=0x%X len=%u failed (rc=%d)", p_driver->p_name,
                                  (unsigned) tx.id, tx.len, TX_RC);
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

                    /* mode=%u — РАЗРЕШЁННЫЙ режим (sul_mode_t после таблицы
                     * приоритетов), остальные поля — сырые ортогональные
                     * сигналы. Оба нужны на стенде: видно и что принёс
                     * декодер, и что из этого выбрал резолвер. */
                    LOG_I(LOG_TAG,
                          "update from %s: pos=%s next=%s dir=%u mode=%u arrival=%d move=%d "
                          "overload=%d fire=%d evac=%d err=%d lading=%d maint=%d fireman=%d "
                          "seismic=%d floor=%u",
                          p_driver->p_name, decoded.pos, decoded.next, (unsigned) decoded.direction,
                          (unsigned) DIFF.mode, decoded.arrival, decoded.movement, decoded.overload,
                          decoded.fire_alarm, decoded.evacuation, decoded.error, decoded.lading,
                          decoded.maintenance, decoded.fireman, decoded.seismic, decoded.floor_num);

                    const render_msg_t MSG = { .task = DIFF, .result = decoded };
                    (void) xQueueOverwrite(g_render_queue, &MSG);
                    if (g_render_task_handle != NULL)
                    {
                        (void) xTaskNotifyGive(g_render_task_handle);
                    }
                }
            }
        }

        vTaskDelayUntil(&last_wake, LOOP_PERIOD);
    }
}

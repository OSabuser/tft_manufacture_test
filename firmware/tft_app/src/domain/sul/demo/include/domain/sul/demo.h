/**
 * @file  demo.h
 * @brief Демо-протокол — синтетический источник данных, не связан с реальной
 *        шиной/станцией (ARCH §8, Фаза 3.3). Витрина возможностей устройства
 *        без СУЛ на другом конце; заодно тест того, что дескрипторный
 *        механизм (§8) и реестр `sul` протокол-агностичны не только для
 *        реального (НКУ-CAN), но и для синтетического источника.
 *
 * Маршрут — скриптованная "поездка" по кругу (согласовано с пользователем):
 * этаж 1 → 11 с промежуточной остановкой на 7 (едет вверх), затем назад
 * 11 → 1 с промежуточной остановкой на 3 (едет вниз), повтор.
 *
 * "Кадр" здесь — формальность, не несёт содержимого (см.
 * sul_transport_demo_receive(), transport/demo.h): decode() САМ ведёт счёт
 * тиков (вызовов) внутри ctx и решает, когда двигать маршрут дальше —
 * остаётся чистой функцией (ctx, вызов) → (новый ctx, результат),
 * host-тестируется как обычный декодер (число тиков → ожидаемый sul_result_t),
 * без привязки к реальному времени (та живёт только в кадансе sul_rx_task).
 */

#ifndef DOMAIN_SUL_DEMO_H_
#define DOMAIN_SUL_DEMO_H_

#include "domain/elevator_model.h"
#include "domain/sul.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    sul_result_t state;
    uint8_t      step;             /**< индекс в маршруте, заворачивается по кругу */
    uint8_t      speed_idx;        /**< 0..2 — медленно/норма/быстро (настройка)    */
    uint16_t     ticks_since_step; /**< счётчик вызовов decode() до следующего шага */
} demo_ctx_t;

/** Сброс: маршрут на первом шаге, скорость — "Норма". */
void demo_init(demo_ctx_t *p_ctx);

/**
 * @brief Задать скорость прохождения маршрута.
 *
 * Вызывать из app-слоя, значение из настроек (proto_slice[0], §8).
 * @param speed_idx 0..2 (медленно/норма/быстро); вне диапазона — клампится к 2.
 */
void demo_set_speed(demo_ctx_t *p_ctx, uint8_t speed_idx);

/** decode() для реестра sul — см. sul_decode_fn в domain/sul.h. Кадр игнорируется. */
sul_status_t demo_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_DEMO_H_ */

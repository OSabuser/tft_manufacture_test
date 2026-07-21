/**
 * @file  controller.h
 * @brief Редьюсер: sul_result_t (+кэш) → indication_task_t (diff).
 *
 * Чистый C, без единого HAL-вызова. НЕ владеет временем/таймаутами сам —
 * caller (app-level sul_rx задача, у неё есть доступ к тику) при обнаружении
 * таймаута связи просто вызывает controller_process() с sul_default_state(),
 * тем же путём, что и любой обычный кадр — специального API для timeout не
 * нужно (см. ARCH.md, поток данных: «poll + timeout→default» — это
 * обязанность вызывающей задачи, не контроллера).
 */

#ifndef DOMAIN_CONTROLLER_H_
#define DOMAIN_CONTROLLER_H_

#include "domain/elevator_model.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Состояние редьюсера — кэш последнего примененного результата. */
typedef struct
{
    sul_result_t cache;
} controller_ctx_t;

/**
 * @brief Что изменилось с прошлого вызова — presentation перерисовывает
 *        только помеченные поля.
 */
typedef struct
{
    bool pos_pending;
    bool direction_pending;
} indication_task_t;

/**
 * @brief Сброс кэша к sul_default_state().
 *
 * Кэш засеивается дефолтом ДО первого реального кадра — если первый кадр от
 * СУЛ совпадёт с дефолтом (напр. пришёл со всё ещё пустой позицией), diff
 * будет "ничего не изменилось". Первую отрисовку (пустой экран → что-то на
 * экране) presentation обязана сделать безусловно один раз при старте, не
 * дожидаясь pending-флагов (см. app: render-задача).
 */
void controller_init(controller_ctx_t *p_ctx);

/**
 * @brief Применить новый результат, вернуть diff относительно кэша.
 *
 * Обновляет кэш на p_result безусловно (следующий вызов будет сравнивать
 * именно с этим состоянием).
 */
indication_task_t controller_process(controller_ctx_t *p_ctx, const sul_result_t *p_result);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_CONTROLLER_H_ */

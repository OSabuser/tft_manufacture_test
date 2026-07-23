/**
 * @file  fallback.h
 * @brief Fallback-рендер (ARCH §11): позиция кабины + стрелка направления,
 *        asset-free, FS-free. Один вкомпилированный шрифт (FloorFontFallback),
 *        примитив стрелки — без TLV/layout-движка (те — Фаза 4/5).
 */

#ifndef UI_FALLBACK_H_
#define UI_FALLBACK_H_

#include "domain/controller.h"
#include "domain/elevator_model.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Диспетчерский вход (opto IN1/IN2, §3.4) — «локальный вход», не
 *        данные СУЛ (ARCH §8 п.3). Самый высокий приоритет из всех режимов —
 *        безусловно перекрывает и обычную индикацию, и любой режим СУЛ
 *        (пожар/перегруз/…), работает даже без связи со станцией. ОТВЕТ
 *        перебивает ВЫЗОВ, если оба почему-то активны одновременно.
 */
typedef enum
{
    DISPATCHER_INDICATION_NONE = 0,
    DISPATCHER_INDICATION_CALL,   /**< «Вызов подан» — opto IN1 */
    DISPATCHER_INDICATION_ANSWER, /**< «Вызов принят» — opto IN2, приоритет выше CALL */
} dispatcher_indication_t;

/**
 * @brief Безусловная первая отрисовка при старте.
 *
 * controller_init() засеивает кэш дефолтом — если первый реальный кадр
 * совпадёт с дефолтом, indication_task_t придёт «ничего не изменилось», и
 * экран останется пустым, если полагаться только на ui_fallback_render().
 * Вызвать один раз при старте до входа в цикл получения кадров.
 *
 * @param dispatcher  текущее состояние диспетчерского входа (см. выше)
 */
void ui_fallback_render_initial(const sul_result_t *p_result, dispatcher_indication_t dispatcher);

/**
 * @brief Инкрементальная перерисовка по diff.
 *
 * Фаза 1: перерисовывает весь кадр целиком при ЛЮБОМ pending-поле (нет
 * partial-update — gfx минимален, без dirty-rect). No-op, если ничего не
 * помечено (dispatcher сюда не входит — его смену обрабатывает отдельный
 * путь в task_render.c через ui_fallback_render_initial(), см. TASKS.md).
 *
 * @param dispatcher  текущее состояние диспетчерского входа (см. выше)
 */
void ui_fallback_render(const indication_task_t *p_task, const sul_result_t *p_result,
                        dispatcher_indication_t dispatcher);

#ifdef __cplusplus
}
#endif

#endif /* UI_FALLBACK_H_ */

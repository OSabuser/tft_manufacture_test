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
 * @brief Безусловная первая отрисовка при старте.
 *
 * controller_init() засеивает кэш дефолтом — если первый реальный кадр
 * совпадёт с дефолтом, indication_task_t придёт «ничего не изменилось», и
 * экран останется пустым, если полагаться только на ui_fallback_render().
 * Вызвать один раз при старте до входа в цикл получения кадров.
 */
void ui_fallback_render_initial(const sul_result_t *p_result);

/**
 * @brief Инкрементальная перерисовка по diff.
 *
 * Фаза 1: перерисовывает весь кадр целиком при ЛЮБОМ pending-поле (нет
 * partial-update — gfx минимален, без dirty-rect). No-op, если ничего не
 * помечено.
 */
void ui_fallback_render(const indication_task_t *p_task, const sul_result_t *p_result);

#ifdef __cplusplus
}
#endif

#endif /* UI_FALLBACK_H_ */

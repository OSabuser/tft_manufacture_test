/**
 * @file  menu_view.h
 * @brief Рендер меню настроек (презентация) — отделён от чистой модели (menu.c).
 *
 * Рисует окно 480×272 в логических (0,0) — одинаково на всех панелях (§PLAN 3.2):
 * заголовок уровня, список пунктов с полосой-курсором, значения, футер-подсказка.
 * Читает только модель (menu_ctx_t) через её запросы; логики навигации не содержит.
 * HIL — проверяется на железе.
 */

#ifndef UI_MENU_VIEW_H_
#define UI_MENU_VIEW_H_

#include "menu/menu.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Отрисовать текущий кадр меню по состоянию модели. Меню должно быть открыто. */
void menu_view_render(const menu_ctx_t *p_ctx);

#ifdef __cplusplus
}
#endif

#endif /* UI_MENU_VIEW_H_ */

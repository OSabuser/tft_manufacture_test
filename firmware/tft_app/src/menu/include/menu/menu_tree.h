/**
 * @file  menu_tree.h
 * @brief Боевое дерево меню Фазы 3 (данные) — Протокол / Адрес / Логи / Выход.
 *
 * Отдельно от движка (menu.c): движок generic, дерево — конкретная конфигурация,
 * привязанная к полям settings_t по offset (§8). Добавить пункт = строка в
 * menu_tree.c, движок не меняется.
 */

#ifndef MENU_MENU_TREE_H_
#define MENU_MENU_TREE_H_

#include "menu/menu.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Дерево пунктов (items[0] — корневое SUBMENU). */
const menu_item_desc_t *menu_tree_items(void);

/** Число пунктов в дереве. */
uint8_t menu_tree_count(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_MENU_TREE_H_ */

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

/**
 * @brief Перестроить секцию "Протокол" дерева из активного sul_settings_desc_t
 *        (ARCH §8) — метки выбора протокола (из реестра) + единственный
 *        параметр активного протокола (label/тип/диапазон/offset/options);
 *        заодно клампит текущее значение параметра под новый диапазон
 *        (proto_slice[0] мог остаться от протокола с более широким
 *        диапазоном — иначе рендер читал бы options[] за границей).
 *
 * @param p_settings_rw  активные настройки (то же, что передано в menu_init());
 *                        функция не сохраняет — только клампит поле на месте.
 *
 * Вызывать: один раз при bringup (после settings_store_load(), до первого
 * открытия меню) и после каждого menu_action() — дёшево (копия нескольких
 * полей + короткий цикл по реестру), тот же паттерн, что переприменение
 * адреса/CAN-фильтров в sul_rx_task.
 */
void menu_tree_refresh_protocol_section(settings_t *p_settings_rw);

#ifdef __cplusplus
}
#endif

#endif /* MENU_MENU_TREE_H_ */

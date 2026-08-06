/**
 * @file  menu.h
 * @brief Движок меню настроек (ARCH.md §8) — ЧИСТАЯ модель, без рендера и QSPI.
 *
 * Дерево пунктов — ДАННЫЕ (плоский массив + индексы parent/child). Редакторы
 * подключаются по типу (menu_item_type_t) — добавить причудливый параметр =
 * добавить тип + строку дерева, движок не меняется (§8, требование
 * расширяемости под клиента).
 *
 * Модель ОПЕРИРУЕТ ПЕРЕДАННЫМ `settings_t*` (мутирует RAM на месте) и **сама не
 * сохраняет** — на выходе-с-сохранением выставляет `save_requested`, а
 * `settings_store_save()` вызывает app-слой. Это держит модель host-тестируемой
 * (без QSPI) и разделяет логику/side-effect по слоям.
 *
 * Навигация: BUTTON_1 → menu_next() (следующий пункт уровня, с заворотом);
 * короткое BUTTON_2 → menu_action() (вход в подменю / инкремент значения /
 * выход-с-сохранением). Вход в меню (короткое BUTTON_1, вне меню) и модальность — уровень app.
 *
 * Соглашение: items[MENU_ROOT_INDEX] — корневое SUBMENU; его дети — верхний
 * уровень (их parent == MENU_ROOT_INDEX). Дети одного уровня — непрерывный
 * диапазон [first_child..last_child].
 */

#ifndef MENU_MENU_H_
#define MENU_MENU_H_

#include "services/settings_store.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MENU_ITEMS_PER_PAGE 6U /* строк на экране (окно 480×272, §PLAN 3.2) */
#define MENU_ROOT_INDEX     0U /* items[0] — корневое SUBMENU               */

/** Тип пункта = редактор. Расширяется добавлением значения (Фазы 5/6:
 *  ARRAY/SERIAL/YEAR/PERCENT/BOOL_ARRAY) — движок не меняется. */
typedef enum
{
    MENU_SUBMENU, /**< вход в подменю [first_child..last_child]           */
    MENU_BACK,    /**< возврат уровнем выше; в корне — выход (+save)       */
    MENU_SELECT,  /**< выбор из списка 0..max (напр. протокол)            */
    MENU_BYTE,    /**< число min..max (напр. адрес)                       */
    MENU_BOOL,    /**< да/нет                                             */
} menu_item_type_t;

/**
 * @brief Пункт меню — данные. Привязка к настройке — байтовый offset uint8-поля
 *        в settings_t (для SELECT/BYTE/BOOL). Для SUBMENU/BACK offset игнорируется.
 */
typedef struct
{
    const char      *label;
    menu_item_type_t type;
    uint16_t         value_offset; /**< offsetof(settings_t, <uint8-поле>)  */
    uint8_t          min;          /**< для SELECT/BYTE/BOOL                */
    uint8_t          max;
    /** Разрыв внутри [min..max]: значения gap_from..gap_to пропускаются при
     *  редактировании (напр. адрес УИМ 1..40 ∪ 46..50 — 41..45 резерв).
     *  gap_from == 0 — разрыва нет. Хранимое значение остаётся настоящим
     *  значением параметра, индекс↔значение не транслируется. */
    uint8_t          gap_from;
    uint8_t          gap_to;
    uint8_t          parent;      /**< индекс родителя (MENU_ROOT_INDEX — верхний уровень) */
    uint8_t          first_child; /**< для SUBMENU — диапазон детей          */
    uint8_t          last_child;
    /** Метки значений для SELECT/BOOL (options[value]); NULL → рендер числом.
     *  Только для презентации — модель (menu.c) это поле не использует. */
    const char *const *options;
} menu_item_desc_t;

/** Состояние навигации — чистое. */
typedef struct
{
    const menu_item_desc_t *items;
    uint8_t                 count;
    settings_t             *settings; /**< мутируется на месте                */
    uint8_t                 cur;      /**< индекс выделенного пункта           */
    uint8_t                 page;     /**< страница для рендера                */
    bool                    open;
    bool                    dirty;          /**< значение менялось              */
    bool                    save_requested; /**< выход-с-сохранением: app зовёт save() */
} menu_ctx_t;

/** Инициализировать (меню закрыто). @p items[MENU_ROOT_INDEX] — корневое SUBMENU. */
void menu_init(menu_ctx_t *p_ctx, const menu_item_desc_t *p_items, uint8_t count,
               settings_t *p_settings);

/** Открыть меню: курсор на первый пункт верхнего уровня, флаги сброшены. */
void menu_open(menu_ctx_t *p_ctx);

/** BUTTON_1: следующий пункт текущего уровня (заворот с последнего на первый). */
void menu_next(menu_ctx_t *p_ctx);

/** Короткое BUTTON_2: вход в подменю / инкремент значения / выход-с-сохранением. */
void menu_action(menu_ctx_t *p_ctx);

bool menu_is_open(const menu_ctx_t *p_ctx);

/* ── Запросы для рендера ─────────────────────────────────────────────────── */

/** Индекс выделенного пункта. */
uint8_t menu_current(const menu_ctx_t *p_ctx);

/** Диапазон пунктов текущего уровня (соседи выделенного) — для отрисовки списка. */
void menu_level_range(const menu_ctx_t *p_ctx, uint8_t *p_first, uint8_t *p_last);

/** Текущее uint8-значение editable-пункта (SELECT/BYTE/BOOL) для отрисовки. */
uint8_t menu_read_value(const menu_ctx_t *p_ctx, uint8_t idx);

#ifdef __cplusplus
}
#endif

#endif /* MENU_MENU_H_ */

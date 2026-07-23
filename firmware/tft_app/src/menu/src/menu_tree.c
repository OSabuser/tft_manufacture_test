#include "menu/menu_tree.h"

#include "domain/sul.h"
#include "services/settings_store.h"

#include <stddef.h>

/* Метки значений (ярус B/устройство). */
static const char *const K_BOOL_LABELS[] = { "Выкл", "Вкл" };

/* Верхняя граница на число протоколов в реестре — только размер буфера меток
 * (menu_tree_refresh_protocol_section), не ограничение самого реестра.
 * Сейчас 2 (НКУ-CAN, демо), с запасом под Фазу 8 (+ УИМ/SD7/УЭЛ/УКЛ — 6). */
#define MENU_TREE_MAX_PROTOCOLS 8U
static const char *s_proto_labels[MENU_TREE_MAX_PROTOCOLS] = { "НКУ-CAN" }; /* фолбэк до refresh() */

/* Индексы пунктов дерева. */
enum
{
    T_ROOT = 0,
    T_PROTO,
    T_PROTO_PARAM, /* единственный параметр АКТИВНОГО протокола (§8) — популируется
                    * из sul_settings_desc_t, см. menu_tree_refresh_protocol_section() */
    T_LOG,
    T_EXIT,
    T_COUNT,
};

/* Боевое дерево Фазы 3. Действуют сейчас: протокол+параметр (→ декодер),
 * логи (→ рантайм-гейт). Остальной каталог настроек — по мере своих фаз.
 *
 * НЕ const: секцию протокола (T_PROTO.max/.options, T_PROTO_PARAM целиком)
 * популирует menu_tree_refresh_protocol_section() из активного
 * sul_settings_desc_t — дескрипторный принцип (§8) применён и к выбору
 * протокола, не только к его параметрам. Значения ниже — safe-фолбэк на
 * случай, если refresh() почему-то не вызван (совпадает с тем, что было
 * до Фазы 3.3, когда протокол был всего один). */
static menu_item_desc_t s_tree[T_COUNT] = {
    [T_ROOT]        = { .label       = "Настройки",
                         .type        = MENU_SUBMENU,
                         .parent      = MENU_ROOT_INDEX,
                         .first_child = T_PROTO,
                         .last_child  = T_EXIT },
    [T_PROTO]       = { .label        = "Протокол",
                         .type         = MENU_SELECT,
                         .value_offset = offsetof(settings_t, device.protocol_id),
                         .min          = 0U,
                         .max          = 0U,
                         .parent       = MENU_ROOT_INDEX,
                         .options      = s_proto_labels },
    [T_PROTO_PARAM] = { .label        = "Адрес",
                         .type         = MENU_BYTE,
                         .value_offset = offsetof(settings_t, user.proto_slice[0]),
                         .min          = 0U,
                         .max          = 15U,
                         .parent       = MENU_ROOT_INDEX },
    [T_LOG]         = { .label        = "Логи",
                         .type         = MENU_BOOL,
                         .value_offset = offsetof(settings_t, device.log_enabled),
                         .min          = 0U,
                         .max          = 1U,
                         .parent       = MENU_ROOT_INDEX,
                         .options      = K_BOOL_LABELS },
    [T_EXIT]        = { .label = "Выход", .type = MENU_BACK, .parent = MENU_ROOT_INDEX },
};

static menu_item_type_t menu_type_from_sul(sul_settings_type_t type)
{
    switch (type)
    {
    case SUL_SETTINGS_SELECT:
        return MENU_SELECT;
    case SUL_SETTINGS_BOOL:
        return MENU_BOOL;
    case SUL_SETTINGS_BYTE:
    default:
        return MENU_BYTE;
    }
}

void menu_tree_refresh_protocol_section(settings_t *p_settings_rw)
{
    /* Метки выбора протокола — имена из реестра, не хардкод (§8). */
    const uint8_t COUNT   = sul_registry_count();
    const uint8_t VISIBLE = (COUNT < MENU_TREE_MAX_PROTOCOLS) ? COUNT : MENU_TREE_MAX_PROTOCOLS;
    for (uint8_t i = 0U; i < VISIBLE; i++)
    {
        const sul_driver_t *p_drv = sul_registry_find(i);
        s_proto_labels[i]         = (p_drv != NULL) ? p_drv->p_name : "?";
    }
    s_tree[T_PROTO].max = (uint8_t) (VISIBLE - 1U);

    /* Единственный параметр активного протокола (§8). Сейчас у каждого
     * зарегистрированного протокола ровно один (НКУ-CAN: адрес; демо:
     * скорость) — N>1 на протокол и скрытие неиспользуемых слотов
     * понадобится Фазе 8, не усложняем заранее (YAGNI). */
    const sul_driver_t *p_active          = sul_registry_active();
    const sul_settings_desc_t *p_settings = p_active->p_settings;

    if ((p_settings != NULL) && (p_settings->count > 0U))
    {
        const sul_settings_entry_t *p_entry = &p_settings->p_entries[0];

        s_tree[T_PROTO_PARAM].label        = p_entry->p_label;
        s_tree[T_PROTO_PARAM].type         = menu_type_from_sul(p_entry->type);
        s_tree[T_PROTO_PARAM].value_offset =
            (uint16_t) (offsetof(settings_t, user.proto_slice) + p_entry->slice_offset);
        s_tree[T_PROTO_PARAM].min     = p_entry->min;
        s_tree[T_PROTO_PARAM].max     = p_entry->max;
        s_tree[T_PROTO_PARAM].options = p_entry->p_options;

        /* Клампим ТЕКУЩЕЕ значение под новый диапазон — proto_slice[0] мог
         * остаться от другого протокола с более широким диапазоном (напр.
         * адрес НКУ-CAN 0..15 -> скорость демо 0..2); без этого рендер читал
         * бы options[value] за пределами массива меток нового протокола. */
        uint8_t *p_val = (uint8_t *) p_settings_rw + s_tree[T_PROTO_PARAM].value_offset;
        if (*p_val > p_entry->max)
        {
            *p_val = p_entry->max;
        }
    }
    else
    {
        /* Протокол без параметров — инертный дефолт (не встречается пока
         * ни у одного зарегистрированного протокола). */
        s_tree[T_PROTO_PARAM].label        = "—";
        s_tree[T_PROTO_PARAM].type         = MENU_BYTE;
        s_tree[T_PROTO_PARAM].value_offset = offsetof(settings_t, user.proto_slice[0]);
        s_tree[T_PROTO_PARAM].min          = 0U;
        s_tree[T_PROTO_PARAM].max          = 0U;
        s_tree[T_PROTO_PARAM].options      = NULL;
    }
}

const menu_item_desc_t *menu_tree_items(void)
{
    return s_tree;
}

uint8_t menu_tree_count(void)
{
    return (uint8_t) T_COUNT;
}

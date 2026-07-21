#include "menu/menu_tree.h"

#include "services/settings_store.h"

#include <stddef.h>

/* Метки значений (ярус B/устройство). */
static const char *const K_BOOL_LABELS[]  = { "Выкл", "Вкл" };
static const char *const K_PROTO_LABELS[] = { "НКУ-CAN" }; /* пока один протокол (§8) */

/* Индексы пунктов дерева. */
enum
{
    T_ROOT = 0,
    T_PROTO,
    T_ADDR,
    T_LOG,
    T_EXIT,
    T_COUNT,
};

/* Боевое дерево Фазы 3. Действуют сейчас: протокол+адрес (→ декодер), логи
 * (→ рантайм-гейт). Остальной каталог настроек — по мере своих фаз. */
static const menu_item_desc_t K_TREE[T_COUNT] = {
    [T_ROOT]  = { .label       = "Настройки",
                  .type        = MENU_SUBMENU,
                  .parent      = MENU_ROOT_INDEX,
                  .first_child = T_PROTO,
                  .last_child  = T_EXIT },
    [T_PROTO] = { .label        = "Протокол",
                  .type         = MENU_SELECT,
                  .value_offset = offsetof(settings_t, device.protocol_id),
                  .min          = 0U,
                  .max          = 0U, /* один протокол; диапазон вырастет в Фазе 8 */
                  .parent       = MENU_ROOT_INDEX,
                  .options      = K_PROTO_LABELS },
    [T_ADDR]  = { .label        = "Адрес",
                  .type         = MENU_BYTE,
                  .value_offset = offsetof(settings_t, user.proto_slice[0]),
                  .min          = 0U,
                  .max          = 15U,
                  .parent       = MENU_ROOT_INDEX },
    [T_LOG]   = { .label        = "Логи",
                  .type         = MENU_BOOL,
                  .value_offset = offsetof(settings_t, device.log_enabled),
                  .min          = 0U,
                  .max          = 1U,
                  .parent       = MENU_ROOT_INDEX,
                  .options      = K_BOOL_LABELS },
    [T_EXIT]  = { .label = "Выход", .type = MENU_BACK, .parent = MENU_ROOT_INDEX },
};

const menu_item_desc_t *menu_tree_items(void)
{
    return K_TREE;
}

uint8_t menu_tree_count(void)
{
    return (uint8_t) T_COUNT;
}

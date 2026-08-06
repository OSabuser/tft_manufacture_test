#include "menu/menu.h"

/* Указатель на uint8-поле настройки, к которому привязан пункт idx. */
static uint8_t *field_ptr(const menu_ctx_t *p_ctx, uint8_t idx)
{
    return (uint8_t *) p_ctx->settings + p_ctx->items[idx].value_offset;
}

static uint8_t page_of(const menu_ctx_t *p_ctx, uint8_t idx, uint8_t first)
{
    return (uint8_t) ((idx - first) / MENU_ITEMS_PER_PAGE);
}

void menu_init(menu_ctx_t *p_ctx, const menu_item_desc_t *p_items, uint8_t count,
               settings_t *p_settings)
{
    p_ctx->items          = p_items;
    p_ctx->count          = count;
    p_ctx->settings       = p_settings;
    p_ctx->cur            = MENU_ROOT_INDEX;
    p_ctx->page           = 0U;
    p_ctx->open           = false;
    p_ctx->dirty          = false;
    p_ctx->save_requested = false;
}

void menu_open(menu_ctx_t *p_ctx)
{
    p_ctx->open           = true;
    p_ctx->dirty          = false;
    p_ctx->save_requested = false;
    p_ctx->cur = p_ctx->items[MENU_ROOT_INDEX].first_child; /* первый пункт верхнего уровня */
    p_ctx->page = 0U;
}

bool menu_is_open(const menu_ctx_t *p_ctx)
{
    return p_ctx->open;
}

uint8_t menu_current(const menu_ctx_t *p_ctx)
{
    return p_ctx->cur;
}

void menu_level_range(const menu_ctx_t *p_ctx, uint8_t *p_first, uint8_t *p_last)
{
    const uint8_t PARENT = p_ctx->items[p_ctx->cur].parent;
    *p_first             = p_ctx->items[PARENT].first_child;
    *p_last              = p_ctx->items[PARENT].last_child;
}

uint8_t menu_read_value(const menu_ctx_t *p_ctx, uint8_t idx)
{
    return *field_ptr(p_ctx, idx);
}

void menu_next(menu_ctx_t *p_ctx)
{
    if (!p_ctx->open)
    {
        return;
    }

    uint8_t first;
    uint8_t last;
    menu_level_range(p_ctx, &first, &last);

    p_ctx->cur  = (p_ctx->cur >= last) ? first : (uint8_t) (p_ctx->cur + 1U); /* заворот */
    p_ctx->page = page_of(p_ctx, p_ctx->cur, first);
}

/* Инкремент editable-значения с заворотом min→max→min и перескоком «разрыва»
 * (см. menu_item_desc_t.gap_from/gap_to — напр. адрес УИМ 1..40 ∪ 46..50:
 * с 40 одно нажатие даёт 46, значения 41..45 недостижимы). */
static void cycle_value(menu_ctx_t *p_ctx)
{
    const menu_item_desc_t *p_it = &p_ctx->items[p_ctx->cur];
    uint8_t *p_v                 = field_ptr(p_ctx, p_ctx->cur);

    uint8_t next = (*p_v >= p_it->max) ? p_it->min : (uint8_t) (*p_v + 1U);

    if ((p_it->gap_from != 0U) && (next >= p_it->gap_from) && (next <= p_it->gap_to))
    {
        next = (uint8_t) (p_it->gap_to + 1U);
        if (next > p_it->max)
        {
            next = p_it->min; /* разрыв упирается в max — заворот */
        }
    }

    *p_v         = next;
    p_ctx->dirty = true;
}

void menu_action(menu_ctx_t *p_ctx)
{
    if (!p_ctx->open)
    {
        return;
    }

    const menu_item_desc_t *p_it = &p_ctx->items[p_ctx->cur];

    switch (p_it->type)
    {
    case MENU_SUBMENU:
        p_ctx->cur  = p_it->first_child;
        p_ctx->page = 0U;
        break;

    case MENU_BACK:
        if (p_it->parent == MENU_ROOT_INDEX)
        {
            /* Корневой выход — сохранить, если что-то менялось. */
            p_ctx->save_requested = p_ctx->dirty;
            p_ctx->open           = false;
        }
        else
        {
            /* Вернуться к пункту-подменю, из которого вошли. */
            p_ctx->cur = p_it->parent;
            uint8_t first;
            uint8_t last;
            menu_level_range(p_ctx, &first, &last);
            p_ctx->page = page_of(p_ctx, p_ctx->cur, first);
        }
        break;

    case MENU_SELECT:
    case MENU_BYTE:
    case MENU_BOOL:
        cycle_value(p_ctx);
        break;
    }
}

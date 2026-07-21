#include "ui/menu_view.h"

#include "services/gfx.h"

#include <stdio.h>

/* Окно 480×272 @ (0,0). Метрики под реальные шрифты: JBMono24 h=31 (заголовок/
 * строки), JBMono12 h=16 (футер). 36 + 6×36 + 20 = 272. */
#define WIN_W        480U
#define TITLE_H      36U
#define ROW_H        36U
#define FOOTER_H     20U
#define ROWS_Y0      TITLE_H
#define FOOTER_Y     (272U - FOOTER_H) /* 252 */
#define TEXT_DY24    2U                 /* (36-31)/2 — центрирование JBMono24 в полосе 36 */
#define TEXT_DY12    2U                 /* (20-16)/2 — JBMono12 в футере 20               */
#define LEFT_MARGIN  16U
#define RIGHT_MARGIN 16U
#define EDGE_MARGIN  8U

/* Палитра (XRGB). Собирается тинтингом из белых шрифтов. */
#define COL_BG       0x000000U
#define COL_TITLE    0x00CFE0FFU
#define COL_LABEL    0x00FFFFFFU
#define COL_VALUE    0x00BFBFBFU
#define COL_SEL_BG   0x0017406BU
#define COL_SEL_TEXT 0x00FFFFFFU
#define COL_FOOTER   0x006F6F6FU
#define COL_SEP      0x00333333U

/* Строка значения пункта. @return false — у пункта нет значения (BACK). */
static bool format_value(const menu_ctx_t *p_ctx, uint8_t idx, char *p_buf, size_t buf_len)
{
    const menu_item_desc_t *p_it = &p_ctx->items[idx];

    switch (p_it->type)
    {
    case MENU_SUBMENU:
        (void) snprintf(p_buf, buf_len, ">");
        return true;
    case MENU_BYTE:
        (void) snprintf(p_buf, buf_len, "%u", menu_read_value(p_ctx, idx));
        return true;
    case MENU_SELECT:
    case MENU_BOOL:
    {
        const uint8_t V = menu_read_value(p_ctx, idx);
        if (p_it->options != NULL)
        {
            (void) snprintf(p_buf, buf_len, "%s", p_it->options[V]);
        }
        else
        {
            (void) snprintf(p_buf, buf_len, "%u", V);
        }
        return true;
    }
    case MENU_BACK:
    default:
        return false;
    }
}

static void draw_row(const menu_ctx_t *p_ctx, uint8_t idx, uint16_t row_y, bool selected)
{
    if (selected)
    {
        gfx_fill_rect(0U, row_y, WIN_W, ROW_H, COL_SEL_BG);
    }

    const gfx_color_t TEXT_COL = selected ? COL_SEL_TEXT : COL_LABEL;
    (void) gfx_draw_string(&SystemFont, p_ctx->items[idx].label, LEFT_MARGIN,
                           (uint16_t) (row_y + TEXT_DY24), TEXT_COL);

    char value[24];
    if (format_value(p_ctx, idx, value, sizeof(value)))
    {
        const uint16_t VW = gfx_string_width(&SystemFont, value);
        const uint16_t VX = (uint16_t) (WIN_W - RIGHT_MARGIN - VW);
        const gfx_color_t VC = selected ? COL_SEL_TEXT : COL_VALUE;
        (void) gfx_draw_string(&SystemFont, value, VX, (uint16_t) (row_y + TEXT_DY24), VC);
    }
}

static void draw_footer(const menu_ctx_t *p_ctx, uint8_t level_first, uint8_t level_last)
{
    gfx_fill_rect(0U, (uint16_t) (FOOTER_Y - 1U), WIN_W, 1U, COL_SEP);

    (void) gfx_draw_string(&SystemFontSmall, "Кн.1 - далее   Кн.2 - выбор", EDGE_MARGIN,
                           (uint16_t) (FOOTER_Y + TEXT_DY12), COL_FOOTER);

    const uint8_t TOTAL = (uint8_t) (level_last - level_first + 1U);
    const uint8_t PAGES = (uint8_t) ((TOTAL + MENU_ITEMS_PER_PAGE - 1U) / MENU_ITEMS_PER_PAGE);
    char page[8];
    (void) snprintf(page, sizeof(page), "%u/%u", (unsigned) (p_ctx->page + 1U), (unsigned) PAGES);
    const uint16_t PW = gfx_string_width(&SystemFontSmall, page);
    (void) gfx_draw_string(&SystemFontSmall, page, (uint16_t) (WIN_W - EDGE_MARGIN - PW),
                           (uint16_t) (FOOTER_Y + TEXT_DY12), COL_FOOTER);
}

void menu_view_render(const menu_ctx_t *p_ctx)
{
    /* Меню модально и переносимо: чёрный весь экран, окно — верхний-левый 480×272. */
    gfx_clear(COL_BG);
    if (!p_ctx->open)
    {
        return;
    }

    /* Заголовок = подпись текущего уровня (родитель выделенного пункта). */
    const char *p_title = p_ctx->items[p_ctx->items[p_ctx->cur].parent].label;
    const uint16_t TW   = gfx_string_width(&SystemFont, p_title);
    (void) gfx_draw_string(&SystemFont, p_title, (uint16_t) ((WIN_W - TW) / 2U), TEXT_DY24, COL_TITLE);
    gfx_fill_rect(0U, (uint16_t) (TITLE_H - 1U), WIN_W, 1U, COL_SEP);

    /* Список пунктов текущего уровня, окно страницы. */
    uint8_t first;
    uint8_t last;
    menu_level_range(p_ctx, &first, &last);

    const uint8_t PAGE_START = (uint8_t) (first + p_ctx->page * MENU_ITEMS_PER_PAGE);
    for (uint8_t slot = 0U; slot < MENU_ITEMS_PER_PAGE; slot++)
    {
        const uint8_t IDX = (uint8_t) (PAGE_START + slot);
        if (IDX > last)
        {
            break;
        }
        const uint16_t ROW_Y = (uint16_t) (ROWS_Y0 + slot * ROW_H);
        draw_row(p_ctx, IDX, ROW_Y, (IDX == p_ctx->cur));
    }

    draw_footer(p_ctx, first, last);
}

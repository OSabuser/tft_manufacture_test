#include "ui/fallback.h"

#include "services/gfx.h"

/* Фаза 1: позиции захардкожены под TFT8 (800×600, panel текущего стенда).
 * Цифра '0' FloorFontFallback — 132×162 px (проверено по сгенерированным
 * данным шрифта). Фаза 5 (layout-движок) заменит на якорное позиционирование,
 * независимое от разрешения панели. */
#define PANEL_WIDTH 800U
#define POS_Y       150U
#define ARROW_Y     380U
#define ARROW_SIZE  80U

static void render(const sul_result_t *p_result)
{
    gfx_clear(GFX_COLOR_BLACK);

    const uint16_t str_width = gfx_string_width(&FloorFontFallback, p_result->pos);
    const uint16_t pos_x     = (uint16_t) ((PANEL_WIDTH - str_width) / 2U);
    (void) gfx_draw_string(&FloorFontFallback, p_result->pos, pos_x, POS_Y);

    /* SUL_DIR_NONE — без стрелки. SUL_DIR_DOUBLE — тоже (не путать с
     * up/down одной стрелкой; отдельная индикация — Фаза 2). */
    if ((p_result->direction == SUL_DIR_UP) || (p_result->direction == SUL_DIR_DOWN))
    {
        const gfx_arrow_dir_t dir = (p_result->direction == SUL_DIR_UP) ? GFX_ARROW_UP : GFX_ARROW_DOWN;
        const uint16_t arrow_x    = (uint16_t) ((PANEL_WIDTH - ARROW_SIZE) / 2U);
        gfx_draw_arrow(dir, arrow_x, ARROW_Y, ARROW_SIZE, GFX_COLOR_WHITE);
    }
}

void ui_fallback_render_initial(const sul_result_t *p_result)
{
    render(p_result);
}

void ui_fallback_render(const indication_task_t *p_task, const sul_result_t *p_result)
{
    if (!p_task->pos_pending && !p_task->direction_pending)
    {
        return;
    }
    render(p_result);
}

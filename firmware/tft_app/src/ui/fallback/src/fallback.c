#include "ui/fallback.h"

#include "domain/mode_priority.h"
#include "services/gfx.h"

#include <stdio.h>

/* Фаза 1/2: позиции захардкожены под TFT8 (800×600, panel текущего стенда).
 * Цифра '0' FloorFontFallback — 132×162 px (проверено по сгенерированным
 * данным шрифта). Фаза 5 (layout-движок) заменит на якорное позиционирование,
 * независимое от разрешения панели. */
#define PANEL_WIDTH 800U
#define POS_Y       150U
#define ARROW_Y     380U
#define ARROW_SIZE  80U
#define MODE_Y      260U /* строка режима (SystemFont), safe-mode индикация */
#define LADING_Y    330U /* обратный отсчёт погрузки (крупным шрифтом)      */

/**
 * @brief Короткая asset-free метка спецрежима (ARCH §11: fallback без
 *        спрайтов/FS). Богатая полноэкранная графика режимов — Фаза 4/5;
 *        здесь — только текст системным шрифтом, чтобы режим был виден и в
 *        safe-mode (без ассетов). NORMAL метки не имеет.
 */
static const char *mode_label(sul_mode_t mode)
{
    switch (mode)
    {
        case SUL_MODE_FIREMAN:     return "ПОЖАРНЫЙ";
        case SUL_MODE_FIRE_ALARM:  return "ПОЖАР";
        case SUL_MODE_OVERLOAD:    return "ПЕРЕГРУЗ";
        case SUL_MODE_SEISMIC:     return "СЕЙСМО";
        case SUL_MODE_MAINTENANCE: return "СЕРВИС";
        case SUL_MODE_LADING:      return "ПОГРУЗКА";
        case SUL_MODE_NORMAL:
        default:                   return NULL;
    }
}

static void draw_centered(const tFont *p_font, const char *p_str, uint16_t y)
{
    const uint16_t width = gfx_string_width(p_font, p_str);
    const uint16_t x     = (uint16_t) ((PANEL_WIDTH - width) / 2U);
    (void) gfx_draw_string(p_font, p_str, x, y, GFX_COLOR_WHITE);
}

static void render_normal(const sul_result_t *p_result)
{
    draw_centered(&FloorFontFallback, p_result->pos, POS_Y);

    /* SUL_DIR_NONE — без стрелки. SUL_DIR_DOUBLE — тоже (двойная стрелка —
     * отдельная спрайтовая индикация Фазы 4/5, в fallback не рисуем). */
    if ((p_result->direction == SUL_DIR_UP) || (p_result->direction == SUL_DIR_DOWN))
    {
        const gfx_arrow_dir_t dir = (p_result->direction == SUL_DIR_UP) ? GFX_ARROW_UP : GFX_ARROW_DOWN;
        const uint16_t arrow_x    = (uint16_t) ((PANEL_WIDTH - ARROW_SIZE) / 2U);
        gfx_draw_arrow(dir, arrow_x, ARROW_Y, ARROW_SIZE, GFX_COLOR_WHITE);
    }
}

static void render(sul_mode_t mode, const sul_result_t *p_result)
{
    gfx_clear();

    const char *label = mode_label(mode);
    if (label == NULL)
    {
        render_normal(p_result);
        return;
    }

    /* Спецрежим: safe-mode текст. Для временной погрузки — ещё и обратный
     * отсчёт крупным шрифтом (lading_secs>0 отличает временную от
     * инструментальной). */
    draw_centered(&SystemFont, label, MODE_Y);

    if ((mode == SUL_MODE_LADING) && (p_result->lading_secs > 0U))
    {
        char secs[6];
        (void) snprintf(secs, sizeof(secs), "%u", (unsigned) p_result->lading_secs);
        draw_centered(&FloorFontFallback, secs, LADING_Y);
    }
}

void ui_fallback_render_initial(const sul_result_t *p_result)
{
    render(sul_resolve_mode(p_result), p_result);
}

void ui_fallback_render(const indication_task_t *p_task, const sul_result_t *p_result)
{
    /* Перерисовываем на изменение того, что fallback реально показывает:
     * позиция, стрелка, режим (+ отсчёт погрузки идёт вместе с mode/pos).
     * next-этаж в safe-mode не рисуется (богатый layout — Фаза 5), поэтому
     * на next_pending не будим. */
    if (!p_task->pos_pending && !p_task->direction_pending && !p_task->mode_pending)
    {
        return;
    }
    render(p_task->mode, p_result);
}

/*
 * Загрузочный экран — рисование (§3.10).
 *
 * Отделено от boot_screen_compose.c: там чистая логика «какие строки», здесь
 * только «куда их положить». На хосте не проверяется — как menu_view.
 */

#include "services/gfx.h"
#include "ui/boot_screen.h"

/* Окно 480×272 в левом верхнем углу — та же переносимая рамка, что у меню
 * (см. menu_view.h): одинаково выглядит на всех панелях, на больших остальное
 * остаётся чёрным. Константы продублированы, а не взяты из menu_view.h —
 * загрузочный экран не должен зависеть от заголовка меню. */
#define WIN_W 480U
#define WIN_H 272U

/* Шрифт — SystemFont (JBMono24, JetBrains Mono 24pt, высота глифа 31): тот же,
 * которым меню рисует свои строки. Загрузочный экран читают с расстояния и
 * второпях, мелкий JBMono12 для этого не годится.
 *
 * Метрики подобраны так, чтобы поместились ВСЕ строки, которые способен выдать
 * компоновщик (BOOT_SCREEN_MAX_LINES = 8), а не только типовые пять:
 *   нижний край последней = TOP_MARGIN + 7*ROW_H + 31 = 12 + 224 + 31 = 267 ≤ 272.
 * При прежних 20/26 (под мелкий шрифт) восьмая строка уехала бы за окно. */
#define LEFT_MARGIN 16U
#define TOP_MARGIN  12U
#define ROW_H       32U

#define COL_BG    GFX_COLOR_BLACK
#define COL_TEXT  GFX_COLOR_WHITE
#define COL_ALERT 0x00FF6060U /* строка «Сброс:» — единственная тревожная */

void ui_boot_screen_render(const boot_screen_info_t *p_info)
{
    boot_screen_lines_t lines;
    boot_screen_compose(p_info, &lines);

    gfx_fill_rect(0U, 0U, WIN_W, WIN_H, COL_BG);

    for (uint8_t i = 0U; i < lines.count; i++)
    {
        const uint16_t Y = (uint16_t) (TOP_MARGIN + ((uint16_t) i * ROW_H));
        if ((Y + ROW_H) > WIN_H)
        {
            break; /* не вылезаем за окно, даже если строк добавят больше */
        }

        /* Строка причины сброса — цветом: она появляется только при нештатном
         * сбросе (см. boot_screen.h) и должна бросаться в глаза. Какая именно
         * строка тревожная, говорит КОМПОНОВЩИК (alert_line) — рендер не
         * анализирует содержимое. */
        const gfx_color_t COL = (i == lines.alert_line) ? COL_ALERT : COL_TEXT;

        (void) gfx_draw_string(&SystemFont, lines.line[i], LEFT_MARGIN, Y, COL);
    }
}

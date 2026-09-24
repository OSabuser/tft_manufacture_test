#include "gfx_blit.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Общий блиттер картинок ──────────────────────────────────────────────────
 *
 * ОДИН пиксельный цикл на всех потребителей: глифы шрифтов, вшитые в образ
 * спрайты резервного режима (§4.1) и — дальше — ассеты из TLV-бандла (§4.2).
 * Различия вынесены во ВХОД, а не в копии кода (требование PLAN §4.1: «две
 * копии пиксельного цикла разъедутся»). Различий ровно два, ортогональных:
 *
 *   ФОРМАТ ИСТОЧНИКА (gfx_image_format_t)
 *     RLE  — поток блоков (см. ниже), так экспортирует lcd-image-converter и
 *            так же будет паковать host-утилита Фазы 4.2;
 *     RAW  — сырые ARGB8888-пиксели подряд (для картинок, которые RLE не жмёт:
 *            фотографические/градиентные — там прогонов нет, см. PLAN §4).
 *
 *   ЦВЕТОВОЙ ПУТЬ (blit_ctx_t.tinted)
 *     ТИНТ — из источника берётся ТОЛЬКО альфа, цвет задаётся параметром.
 *            Так рисуются шрифты (белый глиф любым цветом) и одноцветные
 *            силуэты: один спрайт стрелки годится любой теме, пожарный режим
 *            красится красным без второго файла.
 *     КАК ЕСТЬ — цвет И альфа берутся из пикселя источника (многоцветные
 *            спрайты).
 *
 * RLE-формат (порт draw_char() из OLD_PROJECT source/fonts/fonts.c —
 * проверенный в проде алгоритм). Поток uint32_t, каждый блок начинается с
 * заголовка:
 *   (header & 0xFFFFFF00) == 0xFFFFFF00 → UNIQUE: len = 0x100-(header&0xFF)
 *     уникальных пикселей подряд следуют в потоке (по одному слову каждый).
 *   иначе                                → REPEATABLE: len = header&0xFFFF
 *     повторений ОДНОГО пикселя (следующее слово потока, читается один раз).
 * Пиксели — ARGB8888, порядок row-major, перенос строки на границе width.
 *
 * ВАЖНО: RLE здесь жмёт ПОВТОРЫ полного 32-битного пикселя, а не «однотонность»
 * — прогон из 200 одинаковых красных пикселей сжимается ровно так же, как 200
 * прозрачных. Поэтому плоская многоцветная пиктограмма жмётся не хуже силуэта,
 * и выбор RLE/RAW определяется наличием прогонов, а не числом цветов. ────── */
#define UNIQUE_BLOCK_MASK 0xFFFFFF00U

/* Тинт: белый глиф/силуэт запечён grayscale'ом (0xVVVVVVVV), V одинаков во всех
 * байтах — берём младший как покрытие (α). */
#define TINT_COVERAGE(pixel) ((uint8_t) ((pixel) & 0xFFU))

/* Курсор блита: где рисуем, чем и на какой позиции внутри картинки стоим. */
typedef struct
{
    uint16_t x_pos;
    uint16_t y_pos;
    uint16_t width; /* ширина картинки — по ней переносится строка */
    bool tinted;
    gfx_color_t color; /* значим только при tinted */
    uint32_t col;
    uint32_t row;
} blit_ctx_t;

/* Продвинуть курсор на @p n пикселей БЕЗ отрисовки — для полностью прозрачных
 * прогонов. Деление здесь считается ОДИН раз на прогон, а не на пиксель, и
 * заменяет собой n итераций с записью в некэшируемую SDRAM. На реальном глифе
 * прозрачных пикселей ~69 %, т.е. это не микрооптимизация, а основной выигрыш
 * формата: RLE позволяет пропустить пустоту и в чтении, и в записи. */
static inline void blit_skip(blit_ctx_t *p_ctx, uint32_t n)
{
    const uint32_t FLAT = p_ctx->row * p_ctx->width + p_ctx->col + n;
    p_ctx->row          = FLAT / p_ctx->width;
    p_ctx->col          = FLAT % p_ctx->width;
}

/* Положить один пиксель источника и сдвинуть курсор. Единственное место, где
 * решается цветовой путь. */
static inline void blit_pixel(blit_ctx_t *p_ctx, uint32_t src)
{
    const gfx_color_t COLOR = p_ctx->tinted ? p_ctx->color : (src & 0x00FFFFFFU);
    const uint8_t ALPHA = p_ctx->tinted ? TINT_COVERAGE(src) : (uint8_t) ((src >> 24) & 0xFFU);

    gfx_blend_pixel((uint16_t) (p_ctx->x_pos + p_ctx->col), (uint16_t) (p_ctx->y_pos + p_ctx->row),
                COLOR, ALPHA);

    p_ctx->col++;
    if (p_ctx->col >= p_ctx->width)
    {
        p_ctx->col = 0U;
        p_ctx->row++;
    }
}

/* Полностью прозрачен ли пиксель источника в ТЕКУЩЕМ цветовом пути (в тинте
 * значима младшая компонента, в «как есть» — альфа-байт). */
static inline bool blit_is_transparent(const blit_ctx_t *p_ctx, uint32_t src)
{
    return p_ctx->tinted ? (TINT_COVERAGE(src) == 0U) : (((src >> 24) & 0xFFU) == 0U);
}

static void blit_rle(const tImage *p_image, blit_ctx_t *p_ctx)
{
    const uint32_t TOTAL = (uint32_t) p_image->width * p_image->height;

    uint32_t in_idx = 0U;
    uint32_t out_n  = 0U;

    while (out_n < TOTAL)
    {
        const uint32_t HEADER = p_image->data[in_idx++];

        if ((HEADER & UNIQUE_BLOCK_MASK) == UNIQUE_BLOCK_MASK)
        {
            const uint32_t LEN = 0x100U - (HEADER & 0xFFU);
            for (uint32_t i = 0U; (i < LEN) && (out_n < TOTAL); i++)
            {
                blit_pixel(p_ctx, p_image->data[in_idx]);
                out_n++;
                in_idx++;
            }
        }
        else
        {
            const uint32_t LEN = HEADER & 0xFFFFU;
            /* Имя не SRC — так называется макрос SDK для контроллера сброса
             * (MIMXRT1052_COMMON.h), коллизия ловится только компилятором. */
            const uint32_t SRC_PIXEL = p_image->data[in_idx++];
            const uint32_t RUN       = ((TOTAL - out_n) < LEN) ? (TOTAL - out_n) : LEN;

            if (blit_is_transparent(p_ctx, SRC_PIXEL))
            {
                blit_skip(p_ctx, RUN); /* пустой прогон — ни чтения, ни записи */
            }
            else
            {
                for (uint32_t i = 0U; i < RUN; i++)
                {
                    blit_pixel(p_ctx, SRC_PIXEL);
                }
            }
            out_n += RUN;
        }
    }
}

static void blit_raw(const tImage *p_image, blit_ctx_t *p_ctx)
{
    const uint32_t TOTAL = (uint32_t) p_image->width * p_image->height;

    for (uint32_t i = 0U; i < TOTAL; i++)
    {
        blit_pixel(p_ctx, p_image->data[i]);
    }
}

static void blit_image(const tImage *p_image, gfx_image_format_t format, uint16_t x, uint16_t y,
                       bool tinted, gfx_color_t color)
{
    if ((p_image == NULL) || (p_image->data == NULL) || (p_image->width == 0U) ||
        (p_image->height == 0U))
    {
        return;
    }

    blit_ctx_t ctx = {
        .x_pos  = x,
        .y_pos  = y,
        .width  = p_image->width,
        .tinted = tinted,
        .color  = color,
        .col    = 0U,
        .row    = 0U,
    };

    if (format == GFX_IMAGE_RAW_ARGB8888)
    {
        blit_raw(p_image, &ctx);
    }
    else
    {
        blit_rle(p_image, &ctx);
    }
}

void gfx_draw_image(const tImage *p_image, gfx_image_format_t format, uint16_t x, uint16_t y)
{
    blit_image(p_image, format, x, y, false, 0U);
}

void gfx_draw_image_tinted(const tImage *p_image, gfx_image_format_t format, uint16_t x, uint16_t y,
                           gfx_color_t color)
{
    blit_image(p_image, format, x, y, true, color);
}

/* Глиф шрифта — частный случай общего блита: всегда RLE, всегда тинт. */
void gfx_blit_glyph(const tImage *p_image, uint16_t x, uint16_t y, gfx_color_t color)
{
    blit_image(p_image, GFX_IMAGE_RLE_ARGB8888, x, y, true, color);
}

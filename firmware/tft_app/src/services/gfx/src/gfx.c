#include "services/gfx.h"

#include "fsl_common.h" /* AT_NONCACHEABLE_SECTION_ALIGN */

#include <stddef.h>

/* ── Framebuffer (SDRAM, non-cacheable) ────────────────────────────────────
 *
 * AT_NONCACHEABLE_SECTION_ALIGN размещает переменную в линкер-секции
 * NonCacheable/.ncache → cmake/linker/..._app_slot.ld отображает её на
 * m_sdram_ncache (2 МБ в начале SDRAM), а board_mpu_init() (bsp/generated/
 * board.c, Region 9) конфигурирует ЭТОТ ЖЕ диапазон как non-cacheable через
 * линкер-символы __NCACHE_REGION_START/SIZE — рекомендация NXP для буферов,
 * которые ELCDIF читает по DMA (см. OLD_PROJECT source/display/image_cache.c,
 * тот же макрос, framebuffer/alpha_buffer/processing_buffer). Без этого CPU
 * писал бы через Write-Back D-Cache (Region 8: SDRAM WB Cacheable), и ELCDIF
 * читал бы устаревшие данные, пока кэш-линия не вытеснится сама.
 *
 * Размер — под ТЕКУЩУЮ панель стенда (TFT8, 800×600, известна на Фазе 1), не
 * под BSP_DISPLAY_MAX_*: когда app-big (ARCH §9) станет рантайм-выбирать
 * между TFT7/8/10 в одном бинарнике, размер и m_sdram_ncache (сейчас 2 МБ)
 * придётся поднять до максимума (1024×600×4 ≈ 2.34 МБ → 4 МБ регион). */
#define FRAMEBUFFER_ALIGN 64U /* см. OLD_PROJECT FRAME_BUFFER_ALIGN — типичное ELCDIF/AXI выравнивание */
#define FRAMEBUFFER_PIXELS (800U * 600U)
#define FALLBACK_CHAR '-'

AT_NONCACHEABLE_SECTION_ALIGN(static uint32_t s_framebuffer[FRAMEBUFFER_PIXELS], FRAMEBUFFER_ALIGN);

static uint16_t s_fb_width;
static uint16_t s_fb_height;

static inline void set_pixel(uint16_t x, uint16_t y, gfx_color_t color)
{
    if ((x >= s_fb_width) || (y >= s_fb_height))
    {
        return; /* примитивы могут частично выходить за экран — не UB, просто обрезка */
    }
    s_framebuffer[(uint32_t) y * s_fb_width + x] = color;
}

/* ── Поиск глифа (бинарный — chars[] отсортирован по code, гарантия формата
 * lcd-image-converter) — порт draw_char()+get_character_width() из
 * OLD_PROJECT source/fonts/fonts.c, унифицировано в одну функцию (там был
 * бинарный поиск в draw_char() и отдельный линейный в get_character_width()
 * — один и тот же инвариант сортировки, лишнее дублирование). ─────────── */
static const tImage *find_glyph(const tFont *p_font, long code)
{
    int low  = 0;
    int high = p_font->length - 1;

    while (low <= high)
    {
        const int mid = low + (high - low) / 2;
        if (p_font->chars[mid].code == code)
        {
            return p_font->chars[mid].image;
        }
        if (p_font->chars[mid].code < code)
        {
            low = mid + 1;
        }
        else
        {
            high = mid - 1;
        }
    }
    return NULL;
}

/* ── RLE-декодирование глифа — порт draw_char() из OLD_PROJECT
 * source/fonts/fonts.c (проверенный в проде алгоритм, формат — как
 * реально экспортирует lcd-image-converter, "RLE compression enabled").
 *
 * Поток uint32_t, каждый блок начинается с заголовка:
 *   (header & 0xFFFFFF00) == 0xFFFFFF00 → UNIQUE: len = 0x100-(header&0xFF)
 *     уникальных пикселей подряд следуют в потоке (по одному слову каждый).
 *   иначе                                → REPEATABLE: len = header&0xFFFF
 *     повторений ОДНОГО пикселя (следующее слово потока, читается один раз).
 * Пиксели — ARGB8888, порядок row-major, перенос строки на границе width
 * (advance_pixel в оригинале). ──────────────────────────────────────────── */
#define UNIQUE_BLOCK_MASK 0xFFFFFF00U

static void draw_glyph(const tImage *p_image, uint16_t x_pos, uint16_t y_pos)
{
    const uint32_t total = (uint32_t) p_image->width * p_image->height;

    uint32_t in_idx = 0U;
    uint32_t out_n  = 0U;
    uint32_t col    = 0U;
    uint32_t row    = 0U;

    while (out_n < total)
    {
        const uint32_t header = p_image->data[in_idx++];

        if ((header & UNIQUE_BLOCK_MASK) == UNIQUE_BLOCK_MASK)
        {
            const uint32_t len = 0x100U - (header & 0xFFU);
            for (uint32_t i = 0U; (i < len) && (out_n < total); i++)
            {
                set_pixel((uint16_t) (x_pos + col), (uint16_t) (y_pos + row), p_image->data[in_idx]);
                col++;
                if (col >= p_image->width)
                {
                    col = 0U;
                    row++;
                }
                out_n++;
                in_idx++;
            }
        }
        else
        {
            const uint32_t len   = header & 0xFFFFU;
            const uint32_t pixel = p_image->data[in_idx];
            for (uint32_t i = 0U; (i < len) && (out_n < total); i++)
            {
                set_pixel((uint16_t) (x_pos + col), (uint16_t) (y_pos + row), pixel);
                col++;
                if (col >= p_image->width)
                {
                    col = 0U;
                    row++;
                }
                out_n++;
            }
            in_idx++;
        }
    }
}

/* ── Декодирование одного символа строки, включая 2-байтовый UTF-8
 * (кириллица) — порт логики из OLD_PROJECT draw_string(): ведущие байты
 * 0xD0/0xD1 комбинируются со следующим байтом в один code, как их кодирует
 * lcd-image-converter в tChar.code для таких шрифтов. ──────────────────── */
static long next_codepoint(const char *p_str, uint8_t *p_consumed)
{
    const uint8_t lead = (uint8_t) p_str[0];

    if (((lead == 0xD0U) || (lead == 0xD1U)) && (p_str[1] != '\0'))
    {
        *p_consumed = 2U;
        return ((long) lead << 8) | (uint8_t) p_str[1];
    }

    *p_consumed = 1U;
    return (long) lead;
}

/* find_glyph() с fallback-подстановкой '-' (ARCH §11: по-символьный fallback
 * на отсутствующий в шрифте символ — политика подтверждена: '-' — общий
 * заменитель во FloorFontFallback). Общая для draw_string/string_width. */
static const tImage *find_glyph_with_fallback(const tFont *p_font, long code)
{
    const tImage *p_glyph = find_glyph(p_font, code);
    if (p_glyph == NULL)
    {
        p_glyph = find_glyph(p_font, (long) FALLBACK_CHAR);
    }
    return p_glyph; /* NULL, если даже '-' нет в этом шрифте */
}

uint16_t gfx_draw_string(const tFont *p_font, const char *p_str, uint16_t x, uint16_t y)
{
    if ((p_font == NULL) || (p_str == NULL))
    {
        return 0U;
    }

    uint16_t offset = 0U;

    while (*p_str != '\0')
    {
        uint8_t consumed = 1U;
        const long code   = next_codepoint(p_str, &consumed);
        p_str += consumed;

        const tImage *p_glyph = find_glyph_with_fallback(p_font, code);
        if (p_glyph == NULL)
        {
            continue;
        }

        draw_glyph(p_glyph, (uint16_t) (x + offset), y);
        offset = (uint16_t) (offset + p_glyph->width);
    }

    return offset;
}

uint16_t gfx_string_width(const tFont *p_font, const char *p_str)
{
    if ((p_font == NULL) || (p_str == NULL))
    {
        return 0U;
    }

    uint16_t width = 0U;

    while (*p_str != '\0')
    {
        uint8_t consumed = 1U;
        const long code   = next_codepoint(p_str, &consumed);
        p_str += consumed;

        const tImage *p_glyph = find_glyph_with_fallback(p_font, code);
        if (p_glyph != NULL)
        {
            width = (uint16_t) (width + p_glyph->width);
        }
    }

    return width;
}

/* ── Стрелка — примитив (без спрайтов, ARCH §11 fallback asset-free) ────── */

void gfx_draw_arrow(gfx_arrow_dir_t dir, uint16_t x, uint16_t y, uint16_t size, gfx_color_t color)
{
    if (size < 2U)
    {
        return; /* вырожденный размер — не рисуем, не делим на (size-1)=0 */
    }

    /* Равнобедренный треугольник построчной заливкой: half_width растёт
     * линейно от 0 (вершина) до size/2 (основание). */
    for (uint16_t row = 0U; row < size; row++)
    {
        const uint16_t half_width = (uint16_t) (((uint32_t) row * (size / 2U)) / (size - 1U));
        const uint16_t center     = (uint16_t) (x + size / 2U);
        const uint16_t py = (dir == GFX_ARROW_UP) ? (uint16_t) (y + row) : (uint16_t) (y + (size - 1U) - row);

        for (uint16_t dx = 0U; dx <= half_width; dx++)
        {
            set_pixel((uint16_t) (center - dx), py, color);
            set_pixel((uint16_t) (center + dx), py, color);
        }
    }
}

/* ── Framebuffer / init ──────────────────────────────────────────────────── */

bsp_status_t gfx_init(bsp_display_type_t type)
{
    /* SDRAM (SEMC) — забота вызывающего (bsp_sdram_configure()+init()), gfx
     * владеет только framebuffer'ом внутри уже готовой SDRAM. */
    const bsp_status_t st = bsp_display_init(type, (uint32_t) s_framebuffer, NULL);
    if (st != BSP_OK)
    {
        return st;
    }

    const bsp_display_size_t *p_size = bsp_display_get_size();
    s_fb_width                       = p_size->width;
    s_fb_height                      = p_size->height;

    return BSP_OK;
}

void gfx_clear(gfx_color_t color)
{
    const uint32_t count = (uint32_t) s_fb_width * s_fb_height;

    for (uint32_t i = 0U; i < count; i++)
    {
        s_framebuffer[i] = color;
    }
}

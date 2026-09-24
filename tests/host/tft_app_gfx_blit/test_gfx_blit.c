/**
 * @file  test_gfx_blit.c
 * @brief Host-тесты общего блиттера картинок (services/gfx/src/gfx_blit.c,
 *        Фаза 4.1) — единственного пиксельного цикла в проекте.
 *
 * Почему это вообще тестируется на хосте, хотя «gfx пиксельно не тестируется»
 * (так записано в PLAN про menu_view/fallback). Потому что цикл — чистая
 * логика: разбор RLE-потока, перенос строки, пропуск прозрачных прогонов, два
 * цветовых пути. Железа в нём нет вовсе. Знание о поверхности (AS, её размеры,
 * обрезка по экрану) вынесено за шов `gfx_blend_pixel()`, и ЗДЕСЬ этот шов
 * подменяется на запись в обычный массив.
 *
 * Через этот цикл рисуется ВЕСЬ текст прошивки (глиф шрифта — его частный
 * случай: всегда RLE, всегда тинт), поэтому последний тест — golden на
 * настоящем глифе резервного шрифта: числа получены независимым разбором того
 * же RLE-потока и фиксируют, что декодирование не поехало.
 */

#include "gfx_blit.h"
#include "unity.h"

#include <string.h>

/* ── Шов: подменённый gfx_blend_pixel() пишет в холст ─────────────────────── */

#define CANVAS_W 160U
#define CANVAS_H 200U

static uint8_t s_written[CANVAS_H][CANVAS_W];
static uint8_t s_alpha[CANVAS_H][CANVAS_W];
static gfx_color_t s_color[CANVAS_H][CANVAS_W];
static uint32_t s_calls;
static uint32_t s_out_of_canvas;

/* Реализация шва под тест. Контракт из gfx_blit.h: координаты вне поверхности
 * — тихая обрезка (в прошивке это границы экрана, здесь — границы холста);
 * a == 0 допустим, блиттер имеет право его отдать. */
void gfx_blend_pixel(uint16_t x, uint16_t y, gfx_color_t color, uint8_t a)
{
    s_calls++;

    if ((x >= CANVAS_W) || (y >= CANVAS_H))
    {
        s_out_of_canvas++;
        return;
    }

    s_written[y][x] = 1U;
    s_alpha[y][x]   = a;
    s_color[y][x]   = color;
}

void setUp(void)
{
    (void) memset(s_written, 0, sizeof(s_written));
    (void) memset(s_alpha, 0, sizeof(s_alpha));
    (void) memset(s_color, 0, sizeof(s_color));
    s_calls         = 0U;
    s_out_of_canvas = 0U;
}

void tearDown(void)
{
}

/** Сколько пикселей холста реально получили непрозрачную запись. */
static uint32_t painted(void)
{
    uint32_t n = 0U;
    for (uint32_t y = 0U; y < CANVAS_H; y++)
    {
        for (uint32_t x = 0U; x < CANVAS_W; x++)
        {
            if ((s_written[y][x] != 0U) && (s_alpha[y][x] != 0U))
            {
                n++;
            }
        }
    }
    return n;
}

/* ── RAW: сырые ARGB8888-пиксели подряд ───────────────────────────────────── */

static void test_raw_truecolor_takes_color_and_alpha_from_data(void)
{
    /* 2×2: цвет И альфа берутся из пикселя, строка переносится по width. */
    static const uint32_t DATA[4] = {
        0xFF112233U, 0x80445566U, /* строка 0 */
        0x40778899U, 0xFFAABBCCU, /* строка 1 */
    };
    const tImage IMG = {DATA, 2U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RAW_ARGB8888, 0U, 0U);

    TEST_ASSERT_EQUAL_HEX32(0x00112233U, s_color[0][0]);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, s_alpha[0][0]);
    TEST_ASSERT_EQUAL_HEX32(0x00445566U, s_color[0][1]);
    TEST_ASSERT_EQUAL_UINT8(0x80U, s_alpha[0][1]);
    TEST_ASSERT_EQUAL_HEX32(0x00778899U, s_color[1][0]);
    TEST_ASSERT_EQUAL_UINT8(0x40U, s_alpha[1][0]);
    TEST_ASSERT_EQUAL_HEX32(0x00AABBCCU, s_color[1][1]);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, s_alpha[1][1]);
}

static void test_raw_tinted_takes_alpha_from_low_byte_and_color_from_param(void)
{
    /* Тинт — путь шрифтов и одноцветных силуэтов: из источника значима только
     * альфа (младший байт grayscale-пикселя), цвет задаётся вызывающим. */
    static const uint32_t DATA[2] = {0xFFFFFFFFU, 0x80808080U};
    const tImage IMG              = {DATA, 2U, 1U, 32U};

    gfx_draw_image_tinted(&IMG, GFX_IMAGE_RAW_ARGB8888, 0U, 0U, 0x00FF0000U);

    TEST_ASSERT_EQUAL_HEX32(0x00FF0000U, s_color[0][0]);
    TEST_ASSERT_EQUAL_UINT8(0xFFU, s_alpha[0][0]);
    TEST_ASSERT_EQUAL_HEX32(0x00FF0000U, s_color[0][1]);
    TEST_ASSERT_EQUAL_UINT8(0x80U, s_alpha[0][1]);
}

/* ── RLE: блочный поток ───────────────────────────────────────────────────── */

static void test_rle_repeat_run_fills_and_wraps_rows(void)
{
    /* REPEATABLE: заголовок = длина, следующее слово — сам пиксель (читается
     * ОДИН раз на весь прогон; в этом и экономия трафика из флеша). */
    static const uint32_t DATA[2] = {8U, 0xFF223344U};
    const tImage IMG              = {DATA, 4U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);

    TEST_ASSERT_EQUAL_UINT32(8U, painted());
    for (uint32_t y = 0U; y < 2U; y++)
    {
        for (uint32_t x = 0U; x < 4U; x++)
        {
            TEST_ASSERT_EQUAL_HEX32(0x00223344U, s_color[y][x]);
        }
    }
}

static void test_rle_unique_block_decodes_each_pixel(void)
{
    /* UNIQUE: (header & 0xFFFFFF00) == 0xFFFFFF00, len = 0x100-(header&0xFF).
     * 0xFFFFFFFD → 3 уникальных пикселя следом. */
    static const uint32_t DATA[4] = {0xFFFFFFFDU, 0xFF010101U, 0xFF020202U, 0xFF030303U};
    const tImage IMG              = {DATA, 3U, 1U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);

    TEST_ASSERT_EQUAL_HEX32(0x00010101U, s_color[0][0]);
    TEST_ASSERT_EQUAL_HEX32(0x00020202U, s_color[0][1]);
    TEST_ASSERT_EQUAL_HEX32(0x00030303U, s_color[0][2]);
    TEST_ASSERT_EQUAL_UINT32(3U, painted());
}

static void test_rle_mixed_blocks_continue_one_stream(void)
{
    /* Прогон + уникальные + прогон: курсор обязан оставаться сквозным. */
    static const uint32_t DATA[6] = {
        2U,          0xFF111111U, /* [0],[1] */
        0xFFFFFFFFU, 0xFF222222U, /* [2] — UNIQUE len 1 */
        3U,          0xFF333333U, /* [3],[4],[5] */
    };
    const tImage IMG = {DATA, 3U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);

    TEST_ASSERT_EQUAL_HEX32(0x00111111U, s_color[0][0]);
    TEST_ASSERT_EQUAL_HEX32(0x00111111U, s_color[0][1]);
    TEST_ASSERT_EQUAL_HEX32(0x00222222U, s_color[0][2]);
    TEST_ASSERT_EQUAL_HEX32(0x00333333U, s_color[1][0]);
    TEST_ASSERT_EQUAL_HEX32(0x00333333U, s_color[1][1]);
    TEST_ASSERT_EQUAL_HEX32(0x00333333U, s_color[1][2]);
}

/* ── Пропуск прозрачных прогонов ─────────────────────────────────────────── */

static void test_transparent_run_is_skipped_but_cursor_advances(void)
{
    /* Главный выигрыш формата: прозрачный прогон не читается и не пишется
     * вовсе. Но курсор обязан продвинуться ровно на его длину, иначе
     * следующий блок уедет — именно это ломается при ошибке в арифметике
     * пропуска, и на экране выглядит как «спрайт поехал». */
    static const uint32_t DATA[4] = {
        5U, 0x00000000U, /* 5 прозрачных: [0][0]..[1][0] */
        3U, 0xFF556677U, /* 3 непрозрачных: [1][1],[1][2],[1][3] */
    };
    const tImage IMG = {DATA, 4U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);

    TEST_ASSERT_EQUAL_UINT32(3U, painted());
    TEST_ASSERT_EQUAL_UINT32(0U, s_written[0][0]); /* пропущено, а не «нарисовано прозрачным» */
    TEST_ASSERT_EQUAL_UINT32(0U, s_written[1][0]);
    TEST_ASSERT_EQUAL_HEX32(0x00556677U, s_color[1][1]);
    TEST_ASSERT_EQUAL_HEX32(0x00556677U, s_color[1][2]);
    TEST_ASSERT_EQUAL_HEX32(0x00556677U, s_color[1][3]);

    /* И ни одного лишнего вызова шва на прозрачный прогон. */
    TEST_ASSERT_EQUAL_UINT32(3U, s_calls);
}

static void test_transparency_is_judged_by_active_color_path(void)
{
    /* Один и тот же пиксель прозрачен в одном пути и непрозрачен в другом:
     * 0x000000FF — альфа 0 (прозрачен «как есть»), но младший байт 0xFF
     * (полное покрытие в тинте). Судить обязан ТЕКУЩИЙ путь, иначе спрайт
     * либо исчезнет, либо зальётся сплошняком. */
    static const uint32_t DATA[2] = {4U, 0x000000FFU};
    const tImage IMG              = {DATA, 2U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(0U, painted()); /* «как есть»: альфа 0 → пропуск */

    setUp();

    gfx_draw_image_tinted(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U, 0x0000FF00U);
    TEST_ASSERT_EQUAL_UINT32(4U, painted()); /* тинт: покрытие 0xFF → рисуем */
}

static void test_opaque_alpha_with_zero_low_byte_is_transparent_only_in_tint(void)
{
    /* Обратный случай к предыдущему: 0xFF000000 — чёрный непрозрачный. */
    static const uint32_t DATA[2] = {4U, 0xFF000000U};
    const tImage IMG              = {DATA, 2U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);
    TEST_ASSERT_EQUAL_UINT32(4U, painted()); /* «как есть»: непрозрачный чёрный */

    setUp();

    gfx_draw_image_tinted(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U, 0x0000FF00U);
    TEST_ASSERT_EQUAL_UINT32(0U, painted()); /* тинт: покрытие 0 → пропуск */
}

/* ── Границы и вырожденные входы ──────────────────────────────────────────── */

static void test_run_longer_than_image_is_clamped(void)
{
    /* Битый/усечённый поток не должен писать за пределы картинки: длина
     * прогона обрезается остатком пикселей. */
    static const uint32_t DATA[2] = {1000U, 0xFF010203U};
    const tImage IMG              = {DATA, 2U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);

    TEST_ASSERT_EQUAL_UINT32(4U, painted());
    TEST_ASSERT_EQUAL_UINT32(4U, s_calls);
}

static void test_draw_applies_x_y_offset(void)
{
    static const uint32_t DATA[2] = {4U, 0xFF0A0B0CU};
    const tImage IMG              = {DATA, 2U, 2U, 32U};

    gfx_draw_image(&IMG, GFX_IMAGE_RLE_ARGB8888, 100U, 50U);

    TEST_ASSERT_EQUAL_UINT32(4U, painted());
    TEST_ASSERT_EQUAL_HEX32(0x000A0B0CU, s_color[50][100]);
    TEST_ASSERT_EQUAL_HEX32(0x000A0B0CU, s_color[50][101]);
    TEST_ASSERT_EQUAL_HEX32(0x000A0B0CU, s_color[51][100]);
    TEST_ASSERT_EQUAL_HEX32(0x000A0B0CU, s_color[51][101]);
}

static void test_missing_or_degenerate_image_is_silent_noop(void)
{
    /* Отсутствующий ассет не роняет кадр и не рисует мусор — по-виджетная
     * деградация (ARCH §11). Для §4.1 это ещё и рабочий контракт: спрайта в
     * образе может не быть, тогда рисуется примитив/текст. */
    static const uint32_t DATA[2] = {4U, 0xFF010203U};
    const tImage ZERO_W           = {DATA, 0U, 2U, 32U};
    const tImage ZERO_H           = {DATA, 2U, 0U, 32U};
    const tImage NO_DATA          = {NULL, 2U, 2U, 32U};

    gfx_draw_image(NULL, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);
    gfx_draw_image(&ZERO_W, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);
    gfx_draw_image(&ZERO_H, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);
    gfx_draw_image(&NO_DATA, GFX_IMAGE_RLE_ARGB8888, 0U, 0U);
    gfx_draw_image_tinted(NULL, GFX_IMAGE_RAW_ARGB8888, 0U, 0U, 0x00FFFFFFU);
    gfx_blit_glyph(NULL, 0U, 0U, 0x00FFFFFFU);

    TEST_ASSERT_EQUAL_UINT32(0U, s_calls);
}

/* ── Глиф — частный случай общего блита ──────────────────────────────────── */

static void test_glyph_helper_equals_tinted_rle(void)
{
    static const uint32_t DATA[2] = {4U, 0x80808080U};
    const tImage IMG              = {DATA, 2U, 2U, 32U};

    gfx_blit_glyph(&IMG, 3U, 7U, 0x0000FF00U);

    TEST_ASSERT_EQUAL_UINT32(4U, painted());
    TEST_ASSERT_EQUAL_HEX32(0x0000FF00U, s_color[7][3]);
    TEST_ASSERT_EQUAL_UINT8(0x80U, s_alpha[7][3]);
}

/* ── Golden на настоящем глифе резервного шрифта ──────────────────────────── */

/* FloorFontFallback линкуется целиком (сгенерирован lcd-image-converter,
 * тот же файл, что уезжает в прошивку). Отдельные tImage в нём static —
 * добираемся через публичную таблицу tFont, как это делает gfx.c. */
extern const tFont FloorFontFallback;

#define GLYPH_ZERO_CODE 0x30L

/* Эталоны получены НЕЗАВИСИМЫМ разбором того же RLE-потока (скриптом, по
 * ИСХОДНОМУ алгоритму — до выделения блиттера в отдельный модуль), а не снятием
 * с этой реализации: иначе тест фиксировал бы сам себя. Через этот путь
 * рисуется весь текст прошивки, поэтому расхождение здесь означает, что поехало
 * декодирование шрифтов, а не только спрайтов. */
#define GLYPH_ZERO_W           132U
#define GLYPH_ZERO_H           176U
#define GLYPH_ZERO_OPAQUE      7207U  /* покрытие ровно 0xFF          */
#define GLYPH_ZERO_PARTIAL     33U    /* 0 < покрытие < 0xFF (AA-край) */
#define GLYPH_ZERO_TRANSPARENT 15992U /* покрытие 0 — не рисуется      */

/* Контрольная сумма ПОЗИЦИЙ, а не только количеств. Нужна отдельно: счётчики
 * выше нечувствительны к сдвигу — если курсор перестанет продвигаться на
 * прозрачных прогонах, тот же набор пикселей просто сожмётся к началу картинки,
 * и все три счётчика останутся прежними. Проверено мутацией: без этой суммы
 * поломка пропуска прогонов golden'ом НЕ ловилась. */
#define GLYPH_ZERO_FNV1A 0x5D062996U

static const tImage *find_glyph(long code)
{
    for (int i = 0; i < FloorFontFallback.length; i++)
    {
        if (FloorFontFallback.chars[i].code == code)
        {
            return FloorFontFallback.chars[i].image;
        }
    }
    return NULL;
}

static void test_real_glyph_decodes_to_known_coverage(void)
{
    const tImage *p_glyph = find_glyph(GLYPH_ZERO_CODE);
    TEST_ASSERT_NOT_NULL(p_glyph);
    TEST_ASSERT_EQUAL_UINT16(GLYPH_ZERO_W, p_glyph->width);
    TEST_ASSERT_EQUAL_UINT16(GLYPH_ZERO_H, p_glyph->height);

    const uint16_t X0 = 4U;
    const uint16_t Y0 = 4U;
    gfx_blit_glyph(p_glyph, X0, Y0, 0x00FFFFFFU);

    uint32_t opaque = 0U;
    uint32_t partial = 0U;
    uint32_t transparent = 0U;

    for (uint32_t row = 0U; row < GLYPH_ZERO_H; row++)
    {
        for (uint32_t col = 0U; col < GLYPH_ZERO_W; col++)
        {
            const uint8_t A = (s_written[Y0 + row][X0 + col] != 0U)
                                  ? s_alpha[Y0 + row][X0 + col]
                                  : 0U;
            if (A == 0U)
            {
                transparent++;
            }
            else if (A == 0xFFU)
            {
                opaque++;
            }
            else
            {
                partial++;
            }
        }
    }

    TEST_ASSERT_EQUAL_UINT32(GLYPH_ZERO_OPAQUE, opaque);
    TEST_ASSERT_EQUAL_UINT32(GLYPH_ZERO_PARTIAL, partial);
    TEST_ASSERT_EQUAL_UINT32(GLYPH_ZERO_TRANSPARENT, transparent);
    TEST_ASSERT_EQUAL_UINT32(GLYPH_ZERO_W * GLYPH_ZERO_H, opaque + partial + transparent);
}

/** FNV-1a по (x, y, покрытие) всего прямоугольника глифа — та же свёртка, что
 *  в скрипте-эталоне. Байты подаются в том же порядке, иначе числа не сойдутся. */
static uint32_t glyph_box_fnv1a(uint16_t x0, uint16_t y0)
{
    uint32_t fnv = 0x811C9DC5U;

    for (uint32_t y = 0U; y < GLYPH_ZERO_H; y++)
    {
        for (uint32_t x = 0U; x < GLYPH_ZERO_W; x++)
        {
            const uint8_t A =
                (s_written[y0 + y][x0 + x] != 0U) ? s_alpha[y0 + y][x0 + x] : 0U;
            const uint8_t BYTES[5] = {
                (uint8_t) (x & 0xFFU), (uint8_t) ((x >> 8) & 0xFFU),
                (uint8_t) (y & 0xFFU), (uint8_t) ((y >> 8) & 0xFFU),
                A,
            };

            for (uint32_t i = 0U; i < 5U; i++)
            {
                fnv ^= BYTES[i];
                fnv *= 0x01000193U;
            }
        }
    }

    return fnv;
}

static void test_real_glyph_pixels_land_at_expected_positions(void)
{
    /* Счётчики покрытия выше ловят «нарисовали не то», эта сумма — «нарисовали
     * не там». Разделены намеренно: при провале сразу видно, какого рода
     * поломка (значения пикселей против арифметики курсора). */
    const tImage *p_glyph = find_glyph(GLYPH_ZERO_CODE);
    TEST_ASSERT_NOT_NULL(p_glyph);

    const uint16_t X0 = 4U;
    const uint16_t Y0 = 4U;
    gfx_blit_glyph(p_glyph, X0, Y0, 0x00FFFFFFU);

    TEST_ASSERT_EQUAL_HEX32(GLYPH_ZERO_FNV1A, glyph_box_fnv1a(X0, Y0));
}

static void test_real_glyph_stays_inside_its_box(void)
{
    /* Ни одного пикселя за пределами прямоугольника глифа — проверка той же
     * арифметики курсора, но на реальном потоке с сотнями блоков. */
    const tImage *p_glyph = find_glyph(GLYPH_ZERO_CODE);
    TEST_ASSERT_NOT_NULL(p_glyph);

    const uint16_t X0 = 4U;
    const uint16_t Y0 = 4U;
    gfx_blit_glyph(p_glyph, X0, Y0, 0x00FFFFFFU);

    TEST_ASSERT_EQUAL_UINT32(0U, s_out_of_canvas);

    for (uint32_t y = 0U; y < CANVAS_H; y++)
    {
        for (uint32_t x = 0U; x < CANVAS_W; x++)
        {
            const bool INSIDE = (y >= Y0) && (y < (Y0 + GLYPH_ZERO_H)) && (x >= X0) &&
                                (x < (X0 + GLYPH_ZERO_W));
            if (!INSIDE)
            {
                TEST_ASSERT_EQUAL_UINT32(0U, s_written[y][x]);
            }
        }
    }
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_raw_truecolor_takes_color_and_alpha_from_data);
    RUN_TEST(test_raw_tinted_takes_alpha_from_low_byte_and_color_from_param);

    RUN_TEST(test_rle_repeat_run_fills_and_wraps_rows);
    RUN_TEST(test_rle_unique_block_decodes_each_pixel);
    RUN_TEST(test_rle_mixed_blocks_continue_one_stream);

    RUN_TEST(test_transparent_run_is_skipped_but_cursor_advances);
    RUN_TEST(test_transparency_is_judged_by_active_color_path);
    RUN_TEST(test_opaque_alpha_with_zero_low_byte_is_transparent_only_in_tint);

    RUN_TEST(test_run_longer_than_image_is_clamped);
    RUN_TEST(test_draw_applies_x_y_offset);
    RUN_TEST(test_missing_or_degenerate_image_is_silent_noop);

    RUN_TEST(test_glyph_helper_equals_tinted_rle);

    RUN_TEST(test_real_glyph_decodes_to_known_coverage);
    RUN_TEST(test_real_glyph_pixels_land_at_expected_positions);
    RUN_TEST(test_real_glyph_stays_inside_its_box);

    return UNITY_END();
}

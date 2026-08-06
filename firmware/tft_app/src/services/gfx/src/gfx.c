#include "services/gfx.h"

#include "FreeRTOS.h"
#include "fsl_common.h" /* AT_NONCACHEABLE_SECTION_ALIGN */
#include "fsl_pxp.h"
#include "semphr.h"

#include <stddef.h>
#include <string.h>

/* ── Поверхности компоновщика (SDRAM, non-cacheable) ────────────────────────
 *
 * AT_NONCACHEABLE_SECTION_ALIGN размещает переменную в линкер-секции
 * NonCacheable/.ncache → cmake/linker/..._app_slot.ld отображает её на
 * m_sdram_ncache (8 МБ в начале SDRAM), а board_mpu_init() (bsp/generated/
 * board.c, Region 9) конфигурирует ЭТОТ ЖЕ диапазон как non-cacheable через
 * линкер-символы __NCACHE_REGION_START/SIZE — рекомендация NXP для буферов,
 * которые ELCDIF/PXP читают по DMA (см. OLD_PROJECT source/display/image_cache.c,
 * тот же макрос, framebuffer/alpha_buffer/processing_buffer). Без этого CPU
 * писал бы через Write-Back D-Cache (Region 8: SDRAM WB Cacheable), и DMA
 * читал бы устаревшие данные, пока кэш-линия не вытеснится сама.
 *
 * Четыре поверхности (эталон TFT8_UKL): AS (рисует CPU), PS (фон, чёрный),
 * FB[2] (выход PXP = вход ELCDIF, double buffer). Размер — под ТЕКУЩУЮ панель
 * стенда (TFT8, 800×600), не под BSP_DISPLAY_MAX_*: когда app-big (ARCH §9)
 * станет рантайм-выбирать между TFT7/8/10 в одном бинарнике, размер поверхностей
 * и m_sdram_ncache придётся поднять до максимума панели.
 *
 * ГИБРИД bpp (Фаза 3.2.4): AS — ARGB8888 (8-бит альфа → полноценный AA, примитивы
 * пишут сюда без изменений); PS + оба FB — RGB565 (вдвое меньше полосы: ELCDIF
 * сканирует FB непрерывно, это доминирующая нагрузка на 16-бит SDRAM). PXP читает
 * AS(8888)/PS(565), блендит внутри в ≥8 бит, пишет выход в 565. Форматы поверхностей
 * PXP независимы, поэтому 565-выход НЕ ломает альфа-смешивание. */
#define FRAMEBUFFER_ALIGN                                                                          \
    64U /* см. OLD_PROJECT FRAME_BUFFER_ALIGN — типичное ELCDIF/PXP/AXI выравнивание */
#define SURFACE_PIXELS     (800U * 600U)
#define AS_BYTES_PER_PIXEL 4U /* AS: ARGB8888 */
#define FB_BYTES_PER_PIXEL 2U /* PS + выходные FB: RGB565 */
#define ALPHA_OPAQUE       0xFF000000U /* AS: alpha=0xFF → пиксель непрозрачен для PXP-блендинга */
#define FALLBACK_CHAR      '-'

/* Бюджеты ожиданий в present-пути. Оба ожидания раньше были БЕЗУСЛОВНЫМИ —
 * см. докстрок gfx_pxp_run() про сброс по watchdog без следов в логе. */
#define GFX_PXP_TIMEOUT_MS   500U /* полный композит — ~66 мс (замер), запас x7  */
#define GFX_VSYNC_TIMEOUT_MS 200U /* кадр ELCDIF при 65 Гц — ~15 мс, запас x13   */

/* AS — CPU рисует сюда (ARGB8888, alpha значим). Гибрид: остаётся 32-бит. */
AT_NONCACHEABLE_SECTION_ALIGN(static uint32_t s_alpha_buffer[SURFACE_PIXELS], FRAMEBUFFER_ALIGN);
/* PS — фон под AS (сейчас сплошной чёрный, заливается однократно в gfx_init). RGB565. */
AT_NONCACHEABLE_SECTION_ALIGN(static uint16_t s_processing_buffer[SURFACE_PIXELS],
                              FRAMEBUFFER_ALIGN);
/* Выходные буферы PXP = сканируемые ELCDIF (double buffer, свап в gfx_present). RGB565. */
AT_NONCACHEABLE_SECTION_ALIGN(static uint16_t s_framebuffer[2][SURFACE_PIXELS], FRAMEBUFFER_ALIGN);

static uint16_t s_fb_width;
static uint16_t s_fb_height;

static uint8_t s_back_index; /* индекс FB, в который PXP компонует следующий кадр */
static SemaphoreHandle_t s_frame_done; /* даётся из ELCDIF ISR по завершении кадра          */
/* Счётчик неудачных present'ов (таймаут PXP/vsync или AXI-ошибка) — читает
 * app-слой для диагностики; см. gfx.h. */
volatile uint32_t g_gfx_present_errors;

/* Замер фаз последнего present'а (мс). Диагностика отзывчивости: считаем
 * отдельно busy-wait PXP и ожидание FRAME_DONE — какая фаза съедает время,
 * по логам иначе не различить (см. gfx.h). */
volatile uint32_t g_gfx_last_pxp_ms;
volatile uint32_t g_gfx_last_vsync_ms;

static pxp_output_buffer_config_t s_output_cfg; /* хранится: gfx_present меняет buffer0Addr   */

static inline void set_pixel(uint16_t x, uint16_t y, gfx_color_t color)
{
    if ((x >= s_fb_width) || (y >= s_fb_height))
    {
        return; /* примитивы могут частично выходить за экран — не UB, просто обрезка */
    }
    /* AS: пишем непрозрачно (alpha 0xFF), RGB из color (X-байт игнорируем). */
    s_alpha_buffer[(uint32_t) y * s_fb_width + x] = (color & 0x00FFFFFFU) | ALPHA_OPAQUE;
}

/* Альфа-блендинг: наложить @p color с покрытием @p a (0..255) на текущее
 * содержимое AS. a=0 — пиксель нетронут (прозрачный край глифа), a=255 —
 * полная замена. Так сглаживание глифа корректно ложится на уже нарисованный
 * фон (в т.ч. полосу-курсор), а не штампует чёрный бокс. Результат всегда
 * непрозрачен (alpha 0xFF): PXP покажет его поверх PS. */
static inline void blend_pixel(uint16_t x, uint16_t y, gfx_color_t color, uint8_t a)
{
    if ((x >= s_fb_width) || (y >= s_fb_height) || (a == 0U))
    {
        return;
    }

    const uint32_t idx = (uint32_t) y * s_fb_width + x;
    if (a == 0xFFU)
    {
        s_alpha_buffer[idx] = (color & 0x00FFFFFFU) | ALPHA_OPAQUE;
        return;
    }

    const uint32_t bg   = s_alpha_buffer[idx];
    const uint32_t inv  = 255U - a;
    const uint32_t r    = (((color >> 16) & 0xFFU) * a + ((bg >> 16) & 0xFFU) * inv) / 255U;
    const uint32_t g    = (((color >> 8) & 0xFFU) * a + ((bg >> 8) & 0xFFU) * inv) / 255U;
    const uint32_t b    = (((color) & 0xFFU) * a + ((bg) & 0xFFU) * inv) / 255U;
    s_alpha_buffer[idx] = (r << 16) | (g << 8) | b | ALPHA_OPAQUE;
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

/* Покрытие (α) пикселя глифа — белый глиф запечён grayscale'ом (0xVVVVVVVV),
 * V одинаков во всех байтах; берём младший. */
#define GLYPH_COVERAGE(pixel) ((uint8_t) ((pixel) & 0xFFU))

static void draw_glyph(const tImage *p_image, uint16_t x_pos, uint16_t y_pos, gfx_color_t color)
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
                blend_pixel((uint16_t) (x_pos + col), (uint16_t) (y_pos + row), color,
                            GLYPH_COVERAGE(p_image->data[in_idx]));
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
            const uint32_t len = header & 0xFFFFU;
            const uint8_t a    = GLYPH_COVERAGE(p_image->data[in_idx]);
            for (uint32_t i = 0U; (i < len) && (out_n < total); i++)
            {
                blend_pixel((uint16_t) (x_pos + col), (uint16_t) (y_pos + row), color, a);
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

uint16_t gfx_draw_string(const tFont *p_font, const char *p_str, uint16_t x, uint16_t y,
                         gfx_color_t color)
{
    if ((p_font == NULL) || (p_str == NULL))
    {
        return 0U;
    }

    uint16_t offset = 0U;

    while (*p_str != '\0')
    {
        uint8_t consumed = 1U;
        const long code  = next_codepoint(p_str, &consumed);
        p_str += consumed;

        const tImage *p_glyph = find_glyph_with_fallback(p_font, code);
        if (p_glyph == NULL)
        {
            continue;
        }

        draw_glyph(p_glyph, (uint16_t) (x + offset), y, color);
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
        const long code  = next_codepoint(p_str, &consumed);
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
        const uint16_t py =
            (dir == GFX_ARROW_UP) ? (uint16_t) (y + row) : (uint16_t) (y + (size - 1U) - row);

        for (uint16_t dx = 0U; dx <= half_width; dx++)
        {
            set_pixel((uint16_t) (center - dx), py, color);
            set_pixel((uint16_t) (center + dx), py, color);
        }
    }
}

/* ── Прямоугольники — примитивы (фон/полоса-курсор/разделители меню) ─────── */

/**
 * @brief Залить прямоугольник — ПОСТРОЧНО, как gfx_clear_rect().
 *
 * Раньше здесь был двойной цикл по set_pixel() — то есть на КАЖДЫЙ пиксель
 * вызов функции + две проверки границ + пересчёт индекса с перечитыванием
 * s_fb_width. В Debug-сборке (`-O0`, инлайна нет) это доминировало в стоимости
 * рендера меню: замер на стенде дал draw=940 мс при pxp=8 мс, то есть 99%
 * времени уходило в рисование, а не в композит.
 *
 * Теперь: клампинг ОДИН раз, дальше плотный цикл записи 32-битных слов по
 * строке. Запись идёт в AS (non-cacheable SDRAM), поэтому важно не столько
 * количество инструкций, сколько отсутствие лишних обращений к памяти на
 * пиксель.
 */
void gfx_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, gfx_color_t color)
{
    if ((x >= s_fb_width) || (y >= s_fb_height) || (w == 0U) || (h == 0U))
    {
        return;
    }
    if ((uint32_t) x + w > s_fb_width)
    {
        w = (uint16_t) (s_fb_width - x);
    }
    if ((uint32_t) y + h > s_fb_height)
    {
        h = (uint16_t) (s_fb_height - y);
    }

    const uint32_t PIXEL = (color & 0x00FFFFFFU) | ALPHA_OPAQUE;

    for (uint16_t row = 0U; row < h; row++)
    {
        uint32_t *p_line = &s_alpha_buffer[(uint32_t) (y + row) * s_fb_width + x];
        for (uint16_t i = 0U; i < w; i++)
        {
            p_line[i] = PIXEL;
        }
    }
}

void gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, gfx_color_t color)
{
    if ((w == 0U) || (h == 0U))
    {
        return;
    }

    for (uint16_t i = 0U; i < w; i++)
    {
        set_pixel((uint16_t) (x + i), y, color);
        set_pixel((uint16_t) (x + i), (uint16_t) (y + h - 1U), color);
    }
    for (uint16_t j = 0U; j < h; j++)
    {
        set_pixel(x, (uint16_t) (y + j), color);
        set_pixel((uint16_t) (x + w - 1U), (uint16_t) (y + j), color);
    }
}

/* ── PXP-компоновщик (порт OLD_PROJECT_TFT8_UKL/source/display/pxp_config.c) ── */

/**
 * @brief Настроить поверхности PXP под композит прямоугольника (x,y,w,h).
 *
 * Адреса PS/AS/выхода смещаются к (x,y); pitch остаётся полной строкой
 * поверхности (страйд не меняется) — PXP пишет w×h с шагом полного ряда,
 * попадая ровно в прямоугольник кадра. Позиции PS/AS — (0,0,w,h) относительно
 * выходного кадра w×h (та же конвенция, что была для полного кадра — эталон
 * TFT8_UKL, проверено на железе).
 *
 * Полная конфигурация на КАЖДЫЙ композит (без скрытого состояния между
 * полным и оконным present'ом) — записи регистров PXP дёшевы.
 *
 * @note Текущий потребитель оконного композита — окно меню @ (0,0): смещения
 *       по y кратны полной строке (800×4=3200 Б AS / 800×2=1600 Б FB — кратны
 *       64), выравнивание сохраняется. Для произвольного x следить за
 *       выравниванием адресов под требования PXP.
 */
static void pxp_configure_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t out_addr)
{
    const uint32_t OFFSET = (uint32_t) y * s_fb_width + x;

    /* PS — фон RGB565 (гибрид). Альфа PS в блендинге не участвует (значима альфа AS). */
    const pxp_ps_buffer_config_t ps_cfg = {
        .pixelFormat = kPXP_PsPixelFormatRGB565,
        .swapByte    = false,
        .bufferAddr  = (uint32_t) &s_processing_buffer[OFFSET],
        .bufferAddrU = 0U,
        .bufferAddrV = 0U,
        .pitchBytes  = (uint16_t) (s_fb_width * FB_BYTES_PER_PIXEL),
    };
    PXP_SetProcessSurfaceBufferConfig(PXP, &ps_cfg);

    /* AS — ARGB8888 (гибрид: остаётся 32-бит ради полной 8-бит альфы/AA). */
    const pxp_as_buffer_config_t as_cfg = {
        .pixelFormat = kPXP_AsPixelFormatARGB8888,
        .bufferAddr  = (uint32_t) &s_alpha_buffer[OFFSET],
        .pitchBytes  = (uint16_t) (s_fb_width * AS_BYTES_PER_PIXEL),
    };
    PXP_SetAlphaSurfaceBufferConfig(PXP, &as_cfg);

    /* Выход RGB565 (гибрид): PXP квантует результат бленда в 565 на записи. */
    s_output_cfg.pixelFormat    = kPXP_OutputPixelFormatRGB565;
    s_output_cfg.interlacedMode = kPXP_OutputProgressive;
    s_output_cfg.buffer0Addr    = out_addr;
    s_output_cfg.buffer1Addr    = 0U;
    s_output_cfg.pitchBytes     = (uint16_t) (s_fb_width * FB_BYTES_PER_PIXEL);
    s_output_cfg.width          = w;
    s_output_cfg.height         = h;
    PXP_SetOutputBufferConfig(PXP, &s_output_cfg);

    PXP_SetProcessSurfacePosition(PXP, 0U, 0U, w, h);
    PXP_SetAlphaSurfacePosition(PXP, 0U, 0U, w, h);
}

static void gfx_pxp_init(void)
{
    PXP_Init(PXP);

    /* Embedded alpha: доля AS-пикселя над PS берётся из его альфа-байта.
     * Одноразовая часть конфигурации (per-композит — pxp_configure_rect). */
    const pxp_as_blend_config_t blend_cfg = {
        .alpha       = 0xFFU,
        .invertAlpha = false,
        .alphaMode   = kPXP_AlphaEmbedded,
        .ropMode     = kPXP_RopMaskAs,
    };
    PXP_SetAlphaSurfaceBlendConfig(PXP, &blend_cfg);

    PXP_EnableCsc1(PXP, false); /* включён по умолчанию — фон RGB, конверсия не нужна */

    pxp_configure_rect(0U, 0U, s_fb_width, s_fb_height, (uint32_t) s_framebuffer[0]);
}

/**
 * @brief Запустить PXP и дождаться завершения композиции (busy-wait).
 *
 * ОГРАНИЧЕНО ПО ВРЕМЕНИ И ПРОВЕРЯЕТ ОШИБКИ — так было не всегда, и это стоило
 * боевого бага: раньше здесь стоял `while (!CompleteFlag) {}` без таймаута, а
 * флаги AXI-ошибок сбрасывались перед пуском и после НЕ проверялись. Если PXP
 * упирался в AXI-ошибку, CompleteFlag не появлялся никогда — и этот spin
 * навсегда занимал CPU. Поскольку он исполняется в render_task (приоритет
 * ВЫШЕ sul_rx_task, единственной задачи, кормящей watchdog), плата уходила в
 * сброс через 10 c без единого следа в логе (см. app/crash_log.h).
 *
 * Теперь: истёк бюджет или поднялась AXI-ошибка → возврат false. Кадр
 * пропускается, система остаётся живой и диагностируемой.
 *
 * @return true — композиция завершена; false — таймаут/AXI-ошибка.
 */
static bool gfx_pxp_run(void)
{
    PXP_ClearStatusFlags(PXP, kPXP_CommandLoadFlag);
    PXP_ClearStatusFlags(PXP, kPXP_Axi0ReadErrorFlag);
    PXP_ClearStatusFlags(PXP, kPXP_Axi0WriteErrorFlag);
    PXP_ClearStatusFlags(PXP, kPXP_CompleteFlag);

    PXP_Start(PXP);

    /* Полнокадровый композит на этом железе — ~66 мс (замер, см. PLAN.md
     * Фаза 3.2.4). Бюджет с большим запасом, чтобы не ловить ложных
     * срабатываний на медленных конфигурациях. */
    const TickType_t DEADLINE = xTaskGetTickCount() + pdMS_TO_TICKS(GFX_PXP_TIMEOUT_MS);

    for (;;)
    {
        const uint32_t FLAGS = PXP_GetStatusFlags(PXP);

        if ((FLAGS & (uint32_t) kPXP_CompleteFlag) != 0U)
        {
            return true;
        }

        if ((FLAGS & ((uint32_t) kPXP_Axi0ReadErrorFlag | (uint32_t) kPXP_Axi0WriteErrorFlag)) !=
            0U)
        {
            return false; /* PXP уже не завершится — выходим сразу */
        }

        if ((int32_t) (xTaskGetTickCount() - DEADLINE) >= 0)
        {
            return false;
        }
    }
}

/* ISR-safe: конец кадра ELCDIF → отпустить семафор (синхронизация свапа). */
static void on_frame_done(void)
{
    BaseType_t hp_task_woken = pdFALSE;
    (void) xSemaphoreGiveFromISR(s_frame_done, &hp_task_woken);
    portYIELD_FROM_ISR(hp_task_woken);
}

/* ── Компоновщик / init ──────────────────────────────────────────────────── */

bsp_status_t gfx_init(bsp_display_type_t type)
{
    /* SDRAM (SEMC) — забота вызывающего (bsp_sdram_configure()+init()), gfx
     * владеет только поверхностями внутри уже готовой SDRAM. */
    s_frame_done = xSemaphoreCreateBinary();
    if (s_frame_done == NULL)
    {
        return BSP_ERR_INIT;
    }

    /* PS — чёрный фон; AS — прозрачно; оба выходных FB — чёрные (ELCDIF стартует
     * на FB[0] ещё до первого gfx_present, иначе на экране был бы мусор). */
    (void) memset(s_processing_buffer, 0, sizeof(s_processing_buffer));
    (void) memset(s_alpha_buffer, 0, sizeof(s_alpha_buffer));
    (void) memset(s_framebuffer, 0, sizeof(s_framebuffer));

    /* Семафор создан и колбэк готов ДО включения IRQ внутри bsp_display_init_ex.
     * RGB565 (гибрид): выходные FB 16-бит → ELCDIF-сканаут вдвое дешевле. */
    const bsp_status_t st = bsp_display_init_ex(type, (uint32_t) s_framebuffer[0], on_frame_done,
                                                BSP_DISPLAY_PIXEL_RGB565);
    if (st != BSP_OK)
    {
        return st;
    }

    const bsp_display_size_t *p_size = bsp_display_get_size();
    s_fb_width                       = p_size->width;
    s_fb_height                      = p_size->height;
    s_back_index = 0U; /* FB[0] показывается; первый present уйдёт в FB[1] */

    gfx_pxp_init();

    return BSP_OK;
}

void gfx_clear(void)
{
    /* Прозрачно (alpha 0) → непрорисованные области покажут фон PS (чёрный).
     * Чистим AS (ARGB8888) — 4 байта/пиксель. */
    (void) memset(s_alpha_buffer, 0, (size_t) s_fb_width * s_fb_height * AS_BYTES_PER_PIXEL);
}

/* Общий путь полного и оконного present'а: композит прямоугольника в задний
 * FB + tear-free свап. Прямоугольник должен быть предварительно заклампен. */
static void present_rect_internal(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    /* Компонуем в НЕ показываемый сейчас буфер, показываем атомарным свапом. */
    s_back_index ^= 1U;

    const uint32_t OUT_ADDR =
        (uint32_t) &s_framebuffer[s_back_index][(uint32_t) y * s_fb_width + x];
    pxp_configure_rect(x, y, w, h, OUT_ADDR);

    const TickType_t T_PXP0 = xTaskGetTickCount();
    const bool PXP_OK = gfx_pxp_run(); /* AS над PS → прямоугольник заднего FB */
    g_gfx_last_pxp_ms = (uint32_t) (xTaskGetTickCount() - T_PXP0) * portTICK_PERIOD_MS;

    if (!PXP_OK)
    {
        /* Композит не удался — свап не делаем (в заднем буфере мусор), но и не
         * висим: возвращаем индекс, чтобы следующий кадр снова целился в тот
         * же буфер, и отдаём CPU. Диагностику печатает вызывающий слой. */
        s_back_index ^= 1U;
        g_gfx_present_errors++;
        return;
    }

    /* Синхронизация с развёрткой: дождаться конца кадра, затем отдать ELCDIF
     * новый буфер — он переключится аппаратно на границе кадра (tear-free).
     * Ожидание ОГРАНИЧЕНО: без FRAME_DONE (ELCDIF встал / IRQ потерян) раньше
     * рендер вис здесь навсегда с portMAX_DELAY. Блокирующее ожидание CPU не
     * отнимает, поэтому watchdog это не роняло — но экран замирал молча. */
    const TickType_t T_VS0 = xTaskGetTickCount();
    const BaseType_t VS_RC = xSemaphoreTake(s_frame_done, pdMS_TO_TICKS(GFX_VSYNC_TIMEOUT_MS));
    g_gfx_last_vsync_ms    = (uint32_t) (xTaskGetTickCount() - T_VS0) * portTICK_PERIOD_MS;

    if (VS_RC != pdTRUE)
    {
        g_gfx_present_errors++;
        return; /* свап пропущен; следующий present попробует снова */
    }

    /* ELCDIF всегда получает БАЗОВЫЙ адрес кадра (сканирует весь FB). */
    bsp_display_set_next_buffer((uint32_t) s_framebuffer[s_back_index]);
}

void gfx_present(void)
{
    present_rect_internal(0U, 0U, s_fb_width, s_fb_height);
}

void gfx_present_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if ((x >= s_fb_width) || (y >= s_fb_height) || (w == 0U) || (h == 0U))
    {
        return;
    }
    /* Клампинг по экрану — как у примитивов. */
    if ((uint32_t) x + w > s_fb_width)
    {
        w = (uint16_t) (s_fb_width - x);
    }
    if ((uint32_t) y + h > s_fb_height)
    {
        h = (uint16_t) (s_fb_height - y);
    }

    present_rect_internal(x, y, w, h);
}

void gfx_clear_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if ((x >= s_fb_width) || (y >= s_fb_height) || (w == 0U) || (h == 0U))
    {
        return;
    }
    if ((uint32_t) x + w > s_fb_width)
    {
        w = (uint16_t) (s_fb_width - x);
    }
    if ((uint32_t) y + h > s_fb_height)
    {
        h = (uint16_t) (s_fb_height - y);
    }

    /* Построчный memset региона AS (страйд — полная строка поверхности). */
    for (uint16_t row = 0U; row < h; row++)
    {
        (void) memset(&s_alpha_buffer[(uint32_t) (y + row) * s_fb_width + x], 0,
                      (size_t) w * AS_BYTES_PER_PIXEL);
    }
}

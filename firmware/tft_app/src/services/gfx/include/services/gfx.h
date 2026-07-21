/**
 * @file  gfx.h
 * @brief Фаза 1 — минимальный gfx: framebuffer (SDRAM, non-cacheable) +
 *        RLE-шрифты (lcd-image-converter) + примитив стрелки. Без PXP/
 *        компоновщика/альфа-слоёв — прямая запись в единственный framebuffer,
 *        который ELCDIF сканирует по DMA (см. gfx.c про non-cacheable SDRAM).
 *
 * Формат tImage/tChar/tFont и RLE-декодирование — порт проверенного в проде
 * алгоритма из OLD_PROJECT (source/fonts/fonts.c), тот же формат данных, что
 * реально экспортирует lcd-image-converter (см. services/gfx/fonts/).
 */

#ifndef SERVICES_GFX_H_
#define SERVICES_GFX_H_

#include "bsp/display.h"
#include "bsp/status.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Точный layout, экспортируемый lcd-image-converter — копия
 * services/gfx/fonts/include/fonts.h (тот форсированно инклудится в
 * сгенерированные .c, здесь — то же самое для остального приложения; два
 * файла держать в синхроне, формат внешний и стабилен). */
typedef struct
{
    const uint32_t *data;
    uint16_t width;
    uint16_t height;
    uint8_t dataSize;
} tImage;

typedef struct
{
    long int code;
    const tImage *image;
} tChar;

typedef struct
{
    int length;
    const tChar *chars;
} tFont;

/* Компилируемые в прошивку шрифты (services/gfx/fonts/, сгенерированы
 * lcd-image-converter пользователем). */
extern const tFont FloorFontFallback; /* 0-9, "-", пробел — fallback (§11 ARCH) */
extern const tFont SystemFont;        /* ASCII + кириллица — логи/меню        */

/** XRGB8888 (X игнорируется ELCDIF) — X-байт значения не имеет. */
typedef uint32_t gfx_color_t;

#define GFX_COLOR_BLACK 0x00000000U
#define GFX_COLOR_WHITE 0x00FFFFFFU

/**
 * @brief framebuffer (SDRAM non-cacheable) + bsp_display.
 *
 * SDRAM (SEMC) должна быть уже поднята вызывающим (bsp_sdram_configure() +
 * bsp_sdram_init()) — gfx не владеет SEMC-инициализацией, только framebuffer
 * внутри уже готовой SDRAM.
 *
 * @param type  тип панели (Фаза 1 — хардкод из app; Фаза 9 — provisioning)
 */
bsp_status_t gfx_init(bsp_display_type_t type);

/** Залить весь кадр цветом (обычно GFX_COLOR_BLACK перед перерисовкой). */
void gfx_clear(gfx_color_t color);

/**
 * @brief Нарисовать строку.
 *
 * Символ вне таблицы шрифта (отсутствует в font->chars[]) → подстановка '-'
 * ("-" — согласованный fallback-глиф, см. FloorFontFallback) — по-символьный
 * fallback (ARCH §11), не обрыв/пропуск на первом неизвестном символе. Если
 * даже '-' не найден в переданном шрифте — символ пропускается (ширина 0).
 *
 * Понимает 2-байтовые UTF-8 последовательности (ведущий байт 0xD0/0xD1 —
 * кириллица) как ОДИН символ для подстановки/позиционирования — как и
 * lcd-image-converter кодирует code в tChar для таких шрифтов.
 *
 * @return суммарная ширина отрисованной строки, пиксели.
 */
uint16_t gfx_draw_string(const tFont *p_font, const char *p_str, uint16_t x, uint16_t y);

/** Ширина строки без отрисовки (для центрирования и т.п.). */
uint16_t gfx_string_width(const tFont *p_font, const char *p_str);

typedef enum
{
    GFX_ARROW_UP,
    GFX_ARROW_DOWN,
} gfx_arrow_dir_t;

/** Простая треугольная стрелка — примитив, без спрайтов (ARCH §11: fallback asset-free). */
void gfx_draw_arrow(gfx_arrow_dir_t dir, uint16_t x, uint16_t y, uint16_t size, gfx_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_GFX_H_ */

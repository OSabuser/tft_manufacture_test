/**
 * @file  fonts.h
 * @brief Layout типов, ожидаемый файлами lcd-image-converter (../FloorFontFallback.c,
 *        ../SystemFont.c) — SystemFont.c явно делает #include <fonts.h> (bare name,
 *        так сгенерировал конвертер), FloorFontFallback.c рассчитывает на те же
 *        типы через force-include (см. CMakeLists.txt этой библиотеки).
 *
 * Порядок/типы полей — НЕ менять без пересборки обоих шрифтов в конвертере:
 * они определяют бинарную раскладку структур в сгенерированных .c.
 *
 * Публичный API поверх этого формата — services/gfx.h (там те же типы
 * переобъявлены под неймспейс services/, этот файл — только для компиляции
 * сгенерированных файлов, наружу библиотеки gfx не торчит).
 */

#ifndef FONTS_H_
#define FONTS_H_

#include <stdint.h>

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

#endif /* FONTS_H_ */

/**
 * @file  gfx.h
 * @brief Фаза 3.2.4 — gfx как компоновщик: double-buffer + PXP.
 *
 * Модель (эталон OLD_PROJECT_TFT8_UKL/source/display/): CPU рисует ВЕСЬ кадр в
 * альфа-поверхность **AS** (`alpha_buffer`, ARGB8888; примитивы пишут alpha
 * 0xFF, `gfx_clear` обнуляет → прозрачно). PXP блендит AS над фоновой
 * поверхностью **PS** (`processing_buffer`, сейчас сплошной чёрный) в один из
 * двух задних framebuffer'ов, затем свап синхронно с ELCDIF (семафор FRAME_DONE).
 * Рисуем off-screen, показываем атомарным свапом → tear-free. Стиль-картинка в
 * PS и спрайты в AS — Фаза 4/5; сейчас PS чёрный, композиция = чёрный фон +
 * нарисованное в AS.
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

    /* SystemFont — ASCII + кириллица (логи/меню, метки режимов в fallback).
 * Сгенерированный SystemFont.c экспортирует tFont под именем из .xml
 * конвертера — JBMono24 (JetBrains Mono 24pt). Публичное имя API — SystemFont;
 * связываем алиасом-макросом, не редактируя сгенерированный файл (он
 * перезапишется при регенерации шрифта). Использовать как объект: &SystemFont —
 * симметрично FloorFontFallback. */
    extern const tFont JBMono24;
#define SystemFont JBMono24

    /* SystemFontSmall — мелкий моно (JetBrains Mono 12pt, ASCII+кириллица) для
 * футера меню/подсказок. Тот же приём алиаса, что SystemFont (сгенерированный
 * файл экспортирует tFont под именем JBMono12 из .xml конвертера). */
    extern const tFont JBMono12;
#define SystemFontSmall JBMono12

    /** XRGB8888 (X игнорируется ELCDIF) — X-байт значения не имеет. */
    typedef uint32_t gfx_color_t;

#define GFX_COLOR_BLACK 0x00000000U
#define GFX_COLOR_WHITE 0x00FFFFFFU

    /**
 * @brief Поднять компоновщик: AS/PS/2×FB (SDRAM non-cacheable) + PXP + ELCDIF.
 *
 * Создаёт семафор FRAME_DONE и регистрирует ISR-колбэк ELCDIF (даёт семафор),
 * заливает PS сплошным чёрным, инициализирует PXP (AS над PS → выходной FB) и
 * стартует ELCDIF на FB[0]. До первого gfx_present() экран чёрный.
 *
 * SDRAM (SEMC) должна быть уже поднята вызывающим (bsp_sdram_configure() +
 * bsp_sdram_init()) — gfx не владеет SEMC-инициализацией, только буферами
 * внутри уже готовой SDRAM.
 *
 * @param type  тип панели (Фаза 1 — хардкод из app; Фаза 9 — provisioning)
 */
    bsp_status_t gfx_init(bsp_display_type_t type);

    /** Обнулить AS (весь кадр становится прозрачным). Вызывать перед отрисовкой
 *  нового полного кадра; непрорисованные области покажут фон PS (чёрный). */
    void gfx_clear(void);

    /**
 * @brief Показать нарисованный в AS кадр: PXP-композит AS над PS → задний FB,
 *        затем свап синхронно с ELCDIF (tear-free).
 *
 * Блокирующий (busy-wait завершения PXP + ожидание FRAME_DONE) — звать из
 * задачи-владельца дисплея после того, как полный кадр нарисован в AS.
 */
    void gfx_present(void);

    /**
 * @brief Показать ТОЛЬКО прямоугольник (x,y,w,h): PXP-композит региона AS над
 *        PS → тот же регион заднего FB, затем tear-free свап.
 *
 * Дешевле полного gfx_present() пропорционально площади (окно меню 480×272 —
 * ~27% кадра). Клампится по экрану.
 *
 * @warning КОНТРАКТ ВЫЗЫВАЮЩЕГО: вне прямоугольника задний FB не
 * перекомпоновывается — из-за double buffering там содержимое ДВУХ present'ов
 * назад. Использовать только когда ОБА FB уже содержат корректный кадр вне
 * прямоугольника (модальное меню: на открытии — два полных gfx_present()
 * подряд с одним AS, затем навигация — только окно; см. task_render.c).
 */
    void gfx_present_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    /** Обнулить (сделать прозрачным) только прямоугольник AS — дешёвая замена
 *  полного gfx_clear() для оконной перерисовки (окно меню). Клампится. */
    void gfx_clear_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

    /**
 * @brief Нарисовать строку заданным цветом (тинтинг с альфа-сглаживанием).
 *
 * Глифы шрифта — белые grayscale-покрытием (α = яркость пикселя); отрисовка
 * блендит целевой @p color по этой α с фоном framebuffer'а — так один белый
 * шрифт рисуется любым цветом, а сглаживание корректно ложится и на цветной
 * фон (напр. полосу-курсор меню). Для белого цвета на чёрном фоне результат
 * идентичен «прямой» отрисовке.
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
    uint16_t gfx_draw_string(const tFont *p_font, const char *p_str, uint16_t x, uint16_t y,
                             gfx_color_t color);

    /** Ширина строки без отрисовки (для центрирования и т.п.). */
    uint16_t gfx_string_width(const tFont *p_font, const char *p_str);

    /**
 * @brief Счётчик НЕУДАВШИХСЯ present'ов: таймаут PXP, AXI-ошибка PXP или
 *        потерянный FRAME_DONE.
 *
 * Оба ожидания в present-пути ограничены по времени (раньше были безусловными
 * и один из них — busy-spin'ом, что роняло плату по watchdog без следов).
 * Ненулевое значение = кадры теряются: смотреть питание/тактирование
 * PXP/ELCDIF и адреса поверхностей. Только чтение снаружи.
 */
    extern volatile uint32_t g_gfx_present_errors;

    /** Длительность busy-wait PXP последнего present'а, мс (диагностика). */
    extern volatile uint32_t g_gfx_last_pxp_ms;
    /** Длительность ожидания FRAME_DONE последнего present'а, мс (диагностика). */
    extern volatile uint32_t g_gfx_last_vsync_ms;

    typedef enum
    {
        GFX_ARROW_UP,
        GFX_ARROW_DOWN,
    } gfx_arrow_dir_t;

    /** Простая треугольная стрелка — примитив, без спрайтов (ARCH §11: fallback asset-free). */
    void gfx_draw_arrow(gfx_arrow_dir_t dir, uint16_t x, uint16_t y, uint16_t size,
                        gfx_color_t color);

    /** Залить прямоугольник сплошным цветом (фон, полоса-курсор меню). Обрезается по экрану. */
    void gfx_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, gfx_color_t color);

    /** Контур прямоугольника толщиной 1 px (рамки/разделители). Обрезается по экрану. */
    void gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, gfx_color_t color);

#ifdef __cplusplus
}
#endif

#endif /* SERVICES_GFX_H_ */

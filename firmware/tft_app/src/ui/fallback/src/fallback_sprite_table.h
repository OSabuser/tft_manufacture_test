/**
 * @file  fallback_sprite_table.h
 * @brief Привязка id спрайта → вкомпилированная в образ картинка (Фаза 4.1).
 *        Внутренний заголовок `ui/fallback`, наружу не торчит.
 *
 * Разделение с `fallback_sprites.h` намеренное: ТАМ чистый выбор «состояние →
 * id» (host-тестируется), ЗДЕСЬ — указатели на настоящие `tImage` и то, как их
 * читать. Второе зависит от `services/gfx` и от сгенерированных файлов, то есть
 * на хосте не проверяется и проверяется на стенде.
 *
 * Пустая строка таблицы — НЕ ошибка, а рабочее состояние: спрайта в образе нет
 * → рендер деградирует на примитив/текст (ARCH §11, по-виджетно). Ровно так
 * система живёт до того, как картинки приедут, и ровно так же поведёт себя,
 * если какой-то спрайт решат из образа выкинуть.
 */

#ifndef UI_FALLBACK_SPRITE_TABLE_H_
#define UI_FALLBACK_SPRITE_TABLE_H_

#include "services/gfx.h"
#include "ui/fallback_sprites.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /** Строка таблицы: картинка + как её читать + чем красить. */
    typedef struct
    {
        const tImage *p_image;     /**< NULL — спрайта в образе нет            */
        gfx_image_format_t format; /**< RLE (обычно) или RAW                   */
        bool tinted;               /**< true — цвет из `color`, из PNG только α */
        gfx_color_t color;         /**< значим только при tinted               */
    } fallback_sprite_entry_t;

    /**
 * @brief Строка таблицы для @p id, либо NULL, если картинки нет.
 *
 * NULL — штатный ответ, а не отказ: вызывающий обязан иметь путь деградации.
 */
    const fallback_sprite_entry_t *fallback_sprite_entry(fallback_sprite_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* UI_FALLBACK_SPRITE_TABLE_H_ */

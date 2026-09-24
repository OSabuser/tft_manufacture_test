#include "fallback_sprite_table.h"

#include <stddef.h>

/* ── Внешние картинки, вкомпилированные в образ ───────────────────────────────
 *
 * Сгенерированные паковщиком .c объявляют `const tImage <имя>` — имя совпадает
 * с `fallback_sprite_name()` (одно имя на всю цепочку «PNG → .c → образ → лог»,
 * см. fallback_sprites.h).
 *
 * Пустая строка таблицы — рабочее состояние, а не ошибка: виджет деградирует на
 * примитив/текст. Требования к исходникам — docs/tft_app/FALLBACK_SPRITES.md. */

extern const tImage arrow_up;
extern const tImage arrow_down;
extern const tImage arrow_double;

extern const tImage mode_lading;
extern const tImage mode_maintenance;
extern const tImage mode_seismic;
extern const tImage mode_overload;
extern const tImage mode_fire_alarm;
extern const tImage mode_fireman;
extern const tImage mode_evacuation;
extern const tImage mode_error;

extern const tImage dispatcher_calling;
extern const tImage dispatcher_talking;

/* ── Цветовой путь: по умолчанию «КАК ЕСТЬ», не тинт ──────────────────────────
 *
 * Изначально предполагался тинт (из PNG берётся только альфа, цвет задаёт
 * строка таблицы) — так рисуются шрифты, так лучше сжатие и так цвет менялся бы
 * без перерисовки файлов. Эталонный набор пиктограмм это ОТМЕНИЛ, и не из-за
 * вкуса:
 *
 *  - пиктограммы многоцветны по существу (серый конструктив + красный акцент,
 *    сталь и дерево в инструментах, белое на цветной заливке);
 *  - решающее — ДВЕ ДИСПЕТЧЕРСКИЕ иконки различаются ТОЛЬКО цветом круга
 *    (оранжевый «вызов подан» против зелёного «разговор идёт») при одинаковом
 *    глифе гарнитуры. Под тинтом они стали бы неразличимы на экране, и это
 *    вскрылось бы только на стенде.
 *
 * Поэтому `tinted = false` — цвет и альфа берутся из пикселя. Тинт остаётся
 * доступен ПОСТРОЧНО (поле `tinted` + `color`): если стрелки приедут
 * одноцветными, им он подойдёт — один файл на любую тему.
 *
 * Цветовая палитра прошивки при этом не нужна вовсе: семантика цвета живёт в
 * самих файлах (красный — опасность/останов, оранжевый — вызов подан, зелёный —
 * соединение установлено, серый — конструктив). Константы цветов здесь
 * намеренно НЕ заведены — они появятся только вместе с первым тинтованным
 * спрайтом, а не «на будущее». */

/* ── Таблица ─────────────────────────────────────────────────────────────────
 *
 * Индекс — `fallback_sprite_id_t`. Незаполненные строки нулевые (p_image ==
 * NULL) — ровно то, что означает «спрайта нет». */
#define RLE GFX_IMAGE_RLE_ARGB8888

/* Стрелки — ТИНТ: исходники монохромно-белые (залитый диск с вырезанной
 * стрелкой), значима только альфа. Три следствия: сжатие ×8.8 против ×2.4 у
 * цветных, цвет задаётся здесь и меняется без перерисовки файлов, и попутно
 * игнорируются мусорные RGB-значения внутри фигуры (в исходниках нашлись
 * полностью чёрные пиксели — артефакт lossy-этапа экспорта).
 *
 * Пиктограммы и диспетчер — КАК ЕСТЬ: они многоцветны по существу, а две
 * диспетчерские иконки вообще различаются ТОЛЬКО цветом круга (оранжевый
 * «вызов подан» против зелёного «разговор идёт») при одинаковом глифе. */
#define SPRITE_WHITE 0x00FFFFFFU

static const fallback_sprite_entry_t K_SPRITES[FALLBACK_SPRITE_COUNT] = {
    [FALLBACK_SPRITE_ARROW_UP]     = { &arrow_up, RLE, true, SPRITE_WHITE },
    [FALLBACK_SPRITE_ARROW_DOWN]   = { &arrow_down, RLE, true, SPRITE_WHITE },
    [FALLBACK_SPRITE_ARROW_DOUBLE] = { &arrow_double, RLE, true, SPRITE_WHITE },

    [FALLBACK_SPRITE_MODE_LADING]      = { &mode_lading, RLE, false, 0U },
    [FALLBACK_SPRITE_MODE_MAINTENANCE] = { &mode_maintenance, RLE, false, 0U },
    [FALLBACK_SPRITE_MODE_SEISMIC]     = { &mode_seismic, RLE, false, 0U },
    [FALLBACK_SPRITE_MODE_OVERLOAD]    = { &mode_overload, RLE, false, 0U },
    [FALLBACK_SPRITE_MODE_FIRE_ALARM]  = { &mode_fire_alarm, RLE, false, 0U },
    [FALLBACK_SPRITE_MODE_FIREMAN]     = { &mode_fireman, RLE, false, 0U },
    [FALLBACK_SPRITE_MODE_EVACUATION]  = { &mode_evacuation, RLE, false, 0U },
    [FALLBACK_SPRITE_MODE_ERROR]       = { &mode_error, RLE, false, 0U },

    [FALLBACK_SPRITE_DISPATCHER_CALLING] = { &dispatcher_calling, RLE, false, 0U },
    [FALLBACK_SPRITE_DISPATCHER_TALKING] = { &dispatcher_talking, RLE, false, 0U },
};

const fallback_sprite_entry_t *fallback_sprite_entry(fallback_sprite_id_t id)
{
    if ((unsigned) id >= (unsigned) FALLBACK_SPRITE_COUNT)
    {
        return NULL;
    }
    if (K_SPRITES[id].p_image == NULL)
    {
        return NULL; /* строка не заполнена — картинки в образе нет */
    }
    return &K_SPRITES[id];
}

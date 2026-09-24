#include "ui/fallback_sprites.h"

#include <stddef.h>

/* ── Таблицы-данные ───────────────────────────────────────────────────────────
 *
 * Тот же приём, что в `k_mode_priority[]` (domain/mode_priority.c): строка
 * таблицы вместо ветки в коде. Индекс — значение доменного enum, поэтому
 * добавить режим = дописать строку; забыть строку нельзя незаметно — новый
 * элемент enum даст `FALLBACK_SPRITE_NONE` (нулевая инициализация), и это
 * поймает host-тест `test_every_mode_maps_to_a_sprite`.
 *
 * Designated initializers по значению enum, а не позиционный список: порядок
 * элементов в доменном enum тогда перестаёт иметь значение, а перестановка
 * там не ломает привязку молча. */

static const fallback_sprite_id_t K_BY_DIRECTION[] = {
    [SUL_DIR_NONE]   = FALLBACK_SPRITE_NONE, /* стоим — стрелки нет */
    [SUL_DIR_UP]     = FALLBACK_SPRITE_ARROW_UP,
    [SUL_DIR_DOWN]   = FALLBACK_SPRITE_ARROW_DOWN,
    [SUL_DIR_DOUBLE] = FALLBACK_SPRITE_ARROW_DOUBLE,
};

static const fallback_sprite_id_t K_BY_MODE[] = {
    [SUL_MODE_NORMAL]      = FALLBACK_SPRITE_NONE, /* норма — рисуется этаж, не значок */
    [SUL_MODE_LADING]      = FALLBACK_SPRITE_MODE_LADING,
    [SUL_MODE_MAINTENANCE] = FALLBACK_SPRITE_MODE_MAINTENANCE,
    [SUL_MODE_SEISMIC]     = FALLBACK_SPRITE_MODE_SEISMIC,
    [SUL_MODE_OVERLOAD]    = FALLBACK_SPRITE_MODE_OVERLOAD,
    [SUL_MODE_FIRE_ALARM]  = FALLBACK_SPRITE_MODE_FIRE_ALARM,
    [SUL_MODE_FIREMAN]     = FALLBACK_SPRITE_MODE_FIREMAN,
    [SUL_MODE_EVACUATION]  = FALLBACK_SPRITE_MODE_EVACUATION,
    [SUL_MODE_ERROR]       = FALLBACK_SPRITE_MODE_ERROR,
};

static const fallback_sprite_id_t K_BY_DISPATCHER[] = {
    [DISPATCHER_INDICATION_NONE]    = FALLBACK_SPRITE_NONE,
    [DISPATCHER_INDICATION_CALLING] = FALLBACK_SPRITE_DISPATCHER_CALLING,
    [DISPATCHER_INDICATION_TALKING] = FALLBACK_SPRITE_DISPATCHER_TALKING,
};

/* Имя = имя PNG в каталоге авторинга = имя символа в сгенерированном .c.
 * Держать в синхроне с fallback_sprite_id_t; расхождение ловит host-тест. */
static const char *const K_NAMES[FALLBACK_SPRITE_COUNT] = {
    [FALLBACK_SPRITE_NONE]               = "none",
    [FALLBACK_SPRITE_ARROW_UP]           = "arrow_up",
    [FALLBACK_SPRITE_ARROW_DOWN]         = "arrow_down",
    [FALLBACK_SPRITE_ARROW_DOUBLE]       = "arrow_double",
    [FALLBACK_SPRITE_MODE_LADING]        = "mode_lading",
    [FALLBACK_SPRITE_MODE_MAINTENANCE]   = "mode_maintenance",
    [FALLBACK_SPRITE_MODE_SEISMIC]       = "mode_seismic",
    [FALLBACK_SPRITE_MODE_OVERLOAD]      = "mode_overload",
    [FALLBACK_SPRITE_MODE_FIRE_ALARM]    = "mode_fire_alarm",
    [FALLBACK_SPRITE_MODE_FIREMAN]       = "mode_fireman",
    [FALLBACK_SPRITE_MODE_EVACUATION]    = "mode_evacuation",
    [FALLBACK_SPRITE_MODE_ERROR]         = "mode_error",
    [FALLBACK_SPRITE_DISPATCHER_CALLING] = "dispatcher_calling",
    [FALLBACK_SPRITE_DISPATCHER_TALKING] = "dispatcher_talking",
};

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* Общий поиск по таблице: значение вне диапазона → NONE. Домен и app-слой
 * могут прислать что угодно (протокол-декодер — чужой код, §8 ARCH), и
 * презентация обязана на этом не падать, а тихо не рисовать значок. */
static fallback_sprite_id_t lookup(const fallback_sprite_id_t *p_table, size_t len, unsigned index)
{
    return (index < len) ? p_table[index] : FALLBACK_SPRITE_NONE;
}

fallback_sprite_id_t fallback_sprite_for_direction(sul_direction_t direction)
{
    return lookup(K_BY_DIRECTION, ARRAY_LEN(K_BY_DIRECTION), (unsigned) direction);
}

fallback_sprite_id_t fallback_sprite_for_mode(sul_mode_t mode)
{
    return lookup(K_BY_MODE, ARRAY_LEN(K_BY_MODE), (unsigned) mode);
}

fallback_sprite_id_t fallback_sprite_for_dispatcher(dispatcher_indication_t dispatcher)
{
    return lookup(K_BY_DISPATCHER, ARRAY_LEN(K_BY_DISPATCHER), (unsigned) dispatcher);
}

const char *fallback_sprite_name(fallback_sprite_id_t id)
{
    if (((unsigned) id >= (unsigned) FALLBACK_SPRITE_COUNT) || (K_NAMES[id] == NULL))
    {
        return K_NAMES[FALLBACK_SPRITE_NONE];
    }
    return K_NAMES[id];
}

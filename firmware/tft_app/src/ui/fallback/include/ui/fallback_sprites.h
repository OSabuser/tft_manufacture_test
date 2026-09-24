/**
 * @file  fallback_sprites.h
 * @brief Каталог спрайтов резервного режима и ЧИСТЫЙ выбор спрайта по
 *        состоянию (Фаза 4.1).
 *
 * Здесь только «что рисовать», без «чем и куда»: ни `gfx`, ни `bsp`, ни
 * указателей на картинки. Поэтому выбор host-тестируется целиком, как
 * `sul_resolve_mode()` (domain/mode_priority) — а привязка id → настоящая
 * картинка живёт отдельно, в `fallback_sprite_table.c`, и проверяется на
 * стенде.
 *
 * Почему это вообще отдельный модуль, а не `switch` внутри рендера: набор
 * режимов растёт (PLAN отмечает долг по «перевозке лежачих больных»), и
 * забытая ветка в рендере обнаружилась бы только на объекте. Здесь забытая
 * строка роняет host-тест `test_every_mode_maps_to_a_sprite`.
 */

#ifndef UI_FALLBACK_SPRITES_H_
#define UI_FALLBACK_SPRITES_H_

#include "domain/elevator_model.h"
#include "ui/fallback.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * @brief Каталог спрайтов, вшиваемых В ОБРАЗ (не в TLV-бандл).
 *
 * Граница списка — правило ARCH §11: в образ едет ТОЛЬКО то, что обязано
 * работать, когда бандл недоступен или битый. Спрайт в образе нельзя обновить
 * в поле, поэтому список короткий осознанно: направление, режимы, диспетчер.
 * Всё остальное (лого, фоны, реклама, слайдшоу) — бандл Фазы 4.2.
 *
 * `NONE` означает «спрайт не требуется» (например, режим НОРМА или отсутствие
 * движения), а НЕ «спрайт отсутствует в образе». Второе — свойство таблицы
 * привязки: там строка может быть пустой, и тогда рендер деградирует на
 * примитив/текст (ARCH §11, по-виджетно).
 */
    typedef enum
    {
        FALLBACK_SPRITE_NONE = 0,

        FALLBACK_SPRITE_ARROW_UP,
        FALLBACK_SPRITE_ARROW_DOWN,
        FALLBACK_SPRITE_ARROW_DOUBLE,

        FALLBACK_SPRITE_MODE_LADING,
        FALLBACK_SPRITE_MODE_MAINTENANCE,
        FALLBACK_SPRITE_MODE_SEISMIC,
        FALLBACK_SPRITE_MODE_OVERLOAD,
        FALLBACK_SPRITE_MODE_FIRE_ALARM,
        FALLBACK_SPRITE_MODE_FIREMAN,
        FALLBACK_SPRITE_MODE_EVACUATION,
        FALLBACK_SPRITE_MODE_ERROR,

        FALLBACK_SPRITE_DISPATCHER_CALLING,
        FALLBACK_SPRITE_DISPATCHER_TALKING,

        FALLBACK_SPRITE_COUNT,
    } fallback_sprite_id_t;

    /**
 * @brief Спрайт направления движения.
 *
 * `SUL_DIR_DOUBLE` получает СВОЙ спрайт, а не пару из двух стрелок: в
 * примитивном рендере Фазы 1 двойная стрелка не рисовалась вовсе (пропускалась
 * как «спецрежим индикации»), и это было упрощением примитива, а не решением.
 */
    fallback_sprite_id_t fallback_sprite_for_direction(sul_direction_t direction);

    /** Спрайт экранного режима. `SUL_MODE_NORMAL` → NONE (рисуется этаж). */
    fallback_sprite_id_t fallback_sprite_for_mode(sul_mode_t mode);

    /** Спрайт диспетчерского входа (§3.4). `NONE` → NONE. */
    fallback_sprite_id_t fallback_sprite_for_dispatcher(dispatcher_indication_t dispatcher);

    /**
 * @brief Стабильное имя спрайта — оно же ИМЯ ИСХОДНОГО ФАЙЛА (`<имя>.png`) в
 *        каталоге авторинга и имя символа в сгенерированном `.c`.
 *
 * Одно имя на всю цепочку «макет → паковщик → образ → лог» намеренно: иначе
 * при расхождении картинки на экране и записи в логе непонятно, что чему
 * соответствует. Неизвестный id → "none".
 */
    const char *fallback_sprite_name(fallback_sprite_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* UI_FALLBACK_SPRITES_H_ */

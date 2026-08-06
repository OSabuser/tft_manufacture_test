#include "domain/mode_priority.h"

#include <stddef.h>

/**
 * @brief Строка таблицы приоритетов: булев сигнал (по смещению в sul_result_t)
 *        → экранный режим.
 *
 * Смещение вместо указателя-на-член (в C его нет) — читаем bool по
 * offsetof, что делает таблицу голыми данными без кода на строку.
 */
typedef struct
{
    size_t flag_offset; /**< offsetof(sul_result_t, <bool-поле>) */
    sul_mode_t mode;
} mode_rule_t;

/**
 * ТАБЛИЦА ПРИОРИТЕТОВ (ARCH §7). Порядок = приоритет, сверху вниз.
 * Согласовано: fireman (высший) > пожар > эвакуация > авария > перегруз >
 * сейсмо > сервис > погрузка > (норма — когда ни одна строка не сработала).
 *
 * ЧТО ИМЕННО упорядочивает эта таблица: РЕЖИМЫ между собой — какой из
 * одновременно активных ортогональных сигналов станет `indication_task_t.mode`.
 * Она НЕ говорит «режим скрывает этаж»: режим может выводиться отдельным
 * спрайтом ОДНОВРЕМЕННО с номером этажа и стрелкой — это решает layout
 * (Фазы 4/5). То, что safe-mode fallback рисует вместо этажа текстовую метку,
 * — его собственное упрощение (одна строка, без ассетов), а не правило домена.
 *
 * Эвакуация — в группе «пожарных» режимов, авария («лифт не работает» /
 * неисправность ИБП) — сразу под ней: обе ВЫШЕ перегруза (перегруз временный
 * и разрешается сам, авария означает недоступность лифта). Позиция каждой —
 * одна строка: если у клиента иной регламент, переставить.
 *
 * Изменить приоритет — переставить строки. Добавить режим — дописать строку
 * (offset нового bool-сигнала + его sul_mode_t). Резолвер ниже не меняется.
 * Клиентская кастомизация (§7) — подмена этого массива, без правок кода.
 */
static const mode_rule_t k_mode_priority[] = {
    { offsetof(sul_result_t, fireman), SUL_MODE_FIREMAN },
    { offsetof(sul_result_t, fire_alarm), SUL_MODE_FIRE_ALARM },
    { offsetof(sul_result_t, evacuation), SUL_MODE_EVACUATION },
    { offsetof(sul_result_t, error), SUL_MODE_ERROR },
    { offsetof(sul_result_t, overload), SUL_MODE_OVERLOAD },
    { offsetof(sul_result_t, seismic), SUL_MODE_SEISMIC },
    { offsetof(sul_result_t, maintenance), SUL_MODE_MAINTENANCE },
    { offsetof(sul_result_t, lading), SUL_MODE_LADING },
};

#define MODE_PRIORITY_COUNT (sizeof(k_mode_priority) / sizeof(k_mode_priority[0]))

sul_mode_t sul_resolve_mode(const sul_result_t *p_result)
{
    const uint8_t *p_base = (const uint8_t *) p_result;

    for (size_t i = 0U; i < MODE_PRIORITY_COUNT; ++i)
    {
        const bool ACTIVE = *(const bool *) (p_base + k_mode_priority[i].flag_offset);
        if (ACTIVE)
        {
            return k_mode_priority[i].mode;
        }
    }

    return SUL_MODE_NORMAL;
}

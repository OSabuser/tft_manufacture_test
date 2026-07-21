/**
 * @file  mode_priority.h
 * @brief Свёртка ортогональных сигналов sul_result_t в один экранный режим
 *        по таблице приоритетов (ARCH.md §7).
 *
 * Таблица — ЧИСТЫЕ ДАННЫЕ (offset-массив в mode_priority.c), «потенциально
 * клиентские» (§7): приоритет задаётся ПОРЯДКОМ строк, не значениями enum.
 * Менять приоритет = переставить строки; добавить режим = дописать строку;
 * резолвер при этом не трогается. Контроллер применяет активную таблицу.
 *
 * Чистый C, host-тестируется (tests/host/tft_app_controller).
 */

#ifndef DOMAIN_MODE_PRIORITY_H_
#define DOMAIN_MODE_PRIORITY_H_

#include "domain/elevator_model.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Разрешить экранный режим по активной таблице приоритетов.
 *
 * Проходит таблицу сверху вниз, возвращает режим первой строки, чей булев
 * сигнал в @p p_result выставлен. Если ни один спецрежим не активен —
 * SUL_MODE_NORMAL.
 */
sul_mode_t sul_resolve_mode(const sul_result_t *p_result);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_MODE_PRIORITY_H_ */

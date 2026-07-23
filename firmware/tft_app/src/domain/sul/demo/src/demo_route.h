/**
 * @file  demo_route.h
 * @brief Скриптованный маршрут демо-протокола — ДАННЫЕ, не логика (ARCH §3,
 *        принцип 4: "данные, а не код"). Вынесены отдельно от decode()-логики
 *        (demo.c) для удобства правки — число остановок, диапазон/номера
 *        этажей, спецрежимы правятся здесь, не трогая demo.c.
 *
 * Приватный заголовок (src/, НЕ include/) — используется только demo.c
 * внутри библиотеки tft_app_sul_demo, наружу (registry, app) не торчит.
 */

#ifndef DOMAIN_SUL_DEMO_ROUTE_H_
#define DOMAIN_SUL_DEMO_ROUTE_H_

#include "domain/elevator_model.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Один шаг маршрута.
 *
 * `p_pos` и `floor_num` — РАЗНЫЕ вещи, легко перепутать:
 *   - `p_pos`     — что РИСУЕТСЯ на экране (UTF-8 строка, как `sul_result_t.
 *                   pos`). Может быть что угодно, что умеет активный шрифт:
 *                   "7", "10", "П" (подвал), "-1" — не обязано быть числом.
 *   - `floor_num` — производный ЧИСЛОВОЙ этаж (как `sul_result_t.floor_num`),
 *                   для БУДУЩЕЙ озвучки (Фаза 6, audio_policy) — рендера не
 *                   касается вообще. У обычных этажей совпадает по смыслу с
 *                   `p_pos` (напр. "7" и 7U), но не обязан: нечисловые позиции
 *                   кодируются числом по-своему (см. `floor_number_parser()`
 *                   в nku_can.c — тот же паттерн у реального протокола,
 *                   подвал/минус получают свой числовой диапазон).
 */
typedef struct
{
    const char     *p_pos;
    uint8_t         floor_num;
    sul_direction_t direction;
    bool            arrival; /**< гонг на этом шаге                        */
    sul_mode_t      mode;    /**< спецрежим (SUL_MODE_NORMAL — нет режима) */
} demo_step_t;

#define DEMO_ROUTE_LEN 25U

extern const demo_step_t K_DEMO_ROUTE[DEMO_ROUTE_LEN];

#endif /* DOMAIN_SUL_DEMO_ROUTE_H_ */

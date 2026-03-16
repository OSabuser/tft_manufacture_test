#pragma once

/**
 * @file led.h
 * @brief BSP LED driver — два светодиода на плате (active LOW)
 *
 * LED_HEARTBEAT — системный, мигает как признак жизни прошивки
 * LED_APP       — прикладной, управляется из firmware по ситуации
 *
 * Пины сконфигурированы в generated/pin_mux. Этот хедер не знает
 * ни про GPIO-порты, ни про NXP SDK.
 */

#include <stdbool.h>

/* ── Идентификаторы светодиодов ─────────────────────────────────────── */

typedef enum
{
    LED_HEARTBEAT = 0, /**< системный heartbeat */
    LED_APP       = 1, /**< прикладной индикатор */
} led_id_t;

/* ── API ────────────────────────────────────────────────────────────── */

/**
 * @brief Инициализация обоих светодиодов.
 *        Вызвать один раз после BOARD_InitPins().
 *        После вызова оба LED выключены.
 */
void led_init(void);

/** @brief Включить светодиод. */
void led_on(led_id_t led_id);

/** @brief Выключить светодиод. */
void led_off(led_id_t led_id);

/** @brief Переключить состояние светодиода. */
void led_toggle(led_id_t led_id);

/**
 * @brief Установить состояние светодиода явно.
 * @param on  true — включить, false — выключить
 */
void led_set(led_id_t led_id, bool is_enabled);

/** @brief Получить текущее состояние (true — горит). */
bool led_get(led_id_t led_id);
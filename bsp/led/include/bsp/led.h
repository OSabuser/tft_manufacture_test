#ifndef BSP_LED_H_
#define BSP_LED_H_

/**
 * @file led.h
 * @brief BSP LED driver — два светодиода на плате (active LOW)
 *
 * LED_HEARTBEAT — системный, мигает как признак жизни прошивки
 * LED_APP       — прикладной, управляется из firmware по ситуации
 *
 * Пины сконфигурированы в generated/pin_mux.h. Этот хедер не знает
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
 *        Вызвать один раз после board_hw_init().
 *        После вызова оба LED выключены.
 */
void bsp_led_init(void);

/** @brief Включить светодиод. */
void bsp_led_on(led_id_t led_id);

/** @brief Выключить светодиод. */
void bsp_led_off(led_id_t led_id);

/** @brief Переключить состояние светодиода. */
void bsp_led_toggle(led_id_t led_id);

/**
 * @brief Установить состояние светодиода явно.
 * @param on  true — включить, false — выключить
 */
void bsp_led_set(led_id_t led_id, bool is_enabled);

/** @brief Получить текущее состояние (true — горит). */
bool bsp_led_get(led_id_t led_id);

#endif //BSP_LED_H_
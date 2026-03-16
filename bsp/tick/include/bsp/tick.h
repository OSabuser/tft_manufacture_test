/**
 * @file  bsp/tick.h
 * @brief Монотонный миллисекундный счётчик для BSP.
 *
 * Предоставляет единый API времени для всех BSP-модулей
 * (bsp_uart, bsp_delay, watchdog и др.) в двух режимах сборки:
 *
 * Bare-metal (по умолчанию):
 *   - bsp_tick_init() конфигурирует SysTick на 1 мс.
 *   - SysTick_Handler() в board.c должен вызывать bsp_tick_inc().
 *   - bsp_tick_get_ms() читает внутренний volatile-счётчик.
 *
 * FreeRTOS (#define BSP_TICK_FREERTOS_MODE):
 *   - bsp_tick_init() — no-op (SysTick принадлежит FreeRTOS).
 *   - vApplicationTickHook() в board.c должен вызывать bsp_tick_inc().
 *   - bsp_tick_get_ms() возвращает xTaskGetTickCount().
 *   - Предполагает configTICK_RATE_HZ = 1000 (1 тик = 1 мс).
 *
 * Использование в firmware/CMakeLists.txt:
 *   target_compile_definitions(firmware_test PRIVATE)          // bare-metal
 *   target_compile_definitions(tft_app PRIVATE BSP_TICK_FREERTOS_MODE)
 */

#ifndef BSP_TICK_H
#define BSP_TICK_H

#include <stdint.h>

/* ── Инициализация ───────────────────────────────────────────────────── */

/**
 * @brief Инициализировать источник тиков.
 *
 * Bare-metal: настраивает SysTick на прерывание каждые 1 мс.
 * FreeRTOS:   no-op — SysTick уже настроен планировщиком.
 *
 * Вызывать после board_hw_init(), до любого использования
 * bsp_tick_get_ms() или bsp_delay().
 */
void bsp_tick_init(void);

/* ── Инкремент (из обработчика прерывания) ───────────────────────────── */

/**
 * @brief Увеличить счётчик на 1 мс.
 *
 * Bare-metal:  вызывать из SysTick_Handler().
 * FreeRTOS:    вызывать из vApplicationTickHook().
 *
 * @note В FreeRTOS-режиме функция — no-op (счётчик не используется),
 *       но вызов из hook-а оставляем для единообразия кода board.c.
 */
void bsp_tick_inc(void);

/* ── Чтение времени ──────────────────────────────────────────────────── */

/**
 * @brief Вернуть текущее время в миллисекундах.
 *
 * Bare-metal:  монотонный счётчик, инкрементируемый из SysTick_Handler.
 *              Переполняется через ~49.7 дней (UINT32_MAX мс).
 * FreeRTOS:    возвращает xTaskGetTickCount() (при configTICK_RATE_HZ=1000
 *              значение в мс совпадает с тиками).
 *
 * Для измерения интервалов используй разность:
 *   uint32_t start = bsp_tick_get_ms();
 *   // ... работа ...
 *   uint32_t elapsed = bsp_tick_get_ms() - start;  // корректно при wraparound
 */
uint32_t bsp_tick_get_ms(void);

/* ── Задержка ────────────────────────────────────────────────────────── */

/**
 * @brief Блокирующая задержка.
 *
 * Реализована через polling bsp_tick_get_ms() — не использует
 * RTOS-примитивов, работает в любом контексте включая ISR
 * (при условии что SysTick прерывания не заблокированы).
 *
 * @warning В FreeRTOS-контексте блокирует задачу без освобождения
 *          процессора. Для FreeRTOS предпочитай vTaskDelay().
 *          bsp_delay() здесь — для BSP-инициализации до старта
 *          планировщика и для bare-metal firmware.
 *
 * @param millis  Время задержки в миллисекундах.
 */
void bsp_delay(uint32_t millis);

#endif /* BSP_TICK_H */

/* ── Хук для внешней логики ──────────────────────────────────────────── */

/**
 * @brief Вызывается из SysTick_Handler (bare-metal) или
 *        vApplicationTickHook (FreeRTOS) после bsp_tick_inc().
 *
 * Слабое определение по умолчанию — no-op.
 * Переопредели в своём .c файле для watchdog, программных таймеров и т.п.
 *
 * @warning ISR-контекст. Никаких блокировок и задержек.
 */
void bsp_systick_hook(void);
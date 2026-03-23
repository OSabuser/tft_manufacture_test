/**
 * @file  tick.c
 * @brief Реализация BSP tick — SysTick (bare-metal) или FreeRTOS.
 *
 * SysTick_Handler определён здесь и принадлежит этому модулю.
 * Для добавления внешней логики в обработчик используй bsp_systick_hook().
 */

#include "bsp/tick.h"

/* ── Слабый хук для внешней логики в SysTick ─────────────────────────── */

/**
 * @brief Хук, вызываемый из SysTick_Handler после bsp_tick_inc().
 *
 * По умолчанию — no-op. Переопредяется в своём .c файле (без weak)
 * чтобы добавить периодическую работу: watchdog, программный таймер и т.п.
 *
 * @warning Вызывается из ISR-контекста. Никаких блокировок и задержек.
 */
__attribute__((weak)) void bsp_systick_hook(void)
{ /* no-op */
}

/* ── Bare-metal реализация ───────────────────────────────────────────── */

#ifndef BSP_TICK_FREERTOS_MODE

#include "fsl_common.h"

static volatile uint32_t g_s_tick_ms = 0U;

static const uint16_t MS_DIVIDER = 1000; // Переполнение раз в 1 мс
void bsp_tick_init(void)
{
    SysTick_Config(SystemCoreClock / MS_DIVIDER);
}

void bsp_tick_inc(void)
{
    g_s_tick_ms++;
}

uint32_t bsp_tick_get_ms(void)
{
    return g_s_tick_ms;
}

void SysTick_Handler(void) // NOLINT(readability-identifier-naming)
{
    bsp_tick_inc();
    bsp_systick_hook();
}

/* ── FreeRTOS реализация ─────────────────────────────────────────────── */

#else /* BSP_TICK_FREERTOS_MODE */

#include "FreeRTOS.h"
#include "task.h"

#if configTICK_RATE_HZ != 1000
#warning "BSP_TICK_FREERTOS_MODE: configTICK_RATE_HZ != 1000. \
bsp_tick_get_ms() returns ticks, not milliseconds."
#endif

void bsp_tick_init(void)
{ /* no-op */
}
void bsp_tick_inc(void)
{ /* no-op */
}

uint32_t bsp_tick_get_ms(void)
{
    return (uint32_t) xTaskGetTickCount();
}

/*
 * В FreeRTOS-режиме SysTick_Handler принадлежит планировщику.
 * Периодическая логика идёт через vApplicationTickHook в board.c,
 * который вызывает bsp_tick_inc() (no-op) и bsp_systick_hook().
 *
 * board.c:
 *   void vApplicationTickHook(void) {
 *       bsp_tick_inc();
 *       bsp_systick_hook();
 *   }
 */

#endif /* BSP_TICK_FREERTOS_MODE */

/* ── bsp_delay — общий для обоих режимов ────────────────────────────── */

void bsp_delay(uint32_t millis)
{
    uint32_t start = bsp_tick_get_ms();
    while ((bsp_tick_get_ms() - start) < millis)
    {
        /* busy-wait */
    }
}
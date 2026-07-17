/**
 * @file  led_status.c
 * @brief Реализация словаря LED-паттернов — см. led_status.h и
 *        docs/bootloader/LED_PATTERNS.md.
 */

#include "led_status.h"

#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/wdog.h"

#include <stdbool.h>
#include <stdint.h>

/* ── Периоды паттернов, мс (СИНХРОНИЗИРОВАТЬ с LED_PATTERNS.md) ─────────── */

/** @brief Heartbeat «жив»: 50 мс горит / 450 мс не горит (период 500). */
#define LED_HEARTBEAT_ON_MS     50U
#define LED_HEARTBEAT_PERIOD_MS 500U

/** @brief Неисправность железа: APP 100/100 (период 200). */
#define LED_HW_FAULT_ON_MS     100U
#define LED_HW_FAULT_PERIOD_MS 200U

/** @brief Recovery: оба LED синхронно 100/100 (период 200). */
#define LED_RECOVERY_ON_MS     100U
#define LED_RECOVERY_PERIOD_MS 200U

/** @brief Установка: APP 250/250 (период 500). */
#define LED_INSTALL_ON_MS     250U
#define LED_INSTALL_PERIOD_MS 500U

/** @brief «Образ отклонён»: 4 вспышки по 80 мс вкл / 80 мс выкл. */
#define LED_REJECT_ON_MS 80U
#define LED_REJECT_COUNT 4U

/* ── Состояние модуля ──────────────────────────────────────────────────── */

/** @brief Окно установки открыто — см. led_status_tick_install(). */
static bool g_s_installing = false;

/* ── Внутренние помощники ──────────────────────────────────────────────── */

/** @brief true, если по текущему тику LED в фазе «горит» для период/on. */
static bool phase_on(uint32_t period_ms, uint32_t on_ms)
{
    return (bsp_tick_get_ms() % period_ms) < on_ms;
}

/** @brief Отрисовать системный heartbeat (общий для waiting/hw_fault/install). */
static void draw_heartbeat(void)
{
    bsp_led_set(LED_HEARTBEAT, phase_on(LED_HEARTBEAT_PERIOD_MS, LED_HEARTBEAT_ON_MS));
}

/* ── Public API ────────────────────────────────────────────────────────── */

void led_status_draw_background(led_bg_t bg)
{
    switch (bg)
    {
    case LED_BG_RECOVERY:
    {
        /* Оба LED — один и тот же фазовый расчёт → строго синхронно. */
        const bool ON = phase_on(LED_RECOVERY_PERIOD_MS, LED_RECOVERY_ON_MS);
        bsp_led_set(LED_HEARTBEAT, ON);
        bsp_led_set(LED_APP, ON);
        break;
    }
    case LED_BG_HW_FAULT:
        draw_heartbeat(); /* heartbeat в своём ритме 50/450 — не синхронен с APP */
        bsp_led_set(LED_APP, phase_on(LED_HW_FAULT_PERIOD_MS, LED_HW_FAULT_ON_MS));
        break;
    case LED_BG_WAITING:
    default:
        draw_heartbeat();
        bsp_led_off(LED_APP);
        break;
    }
}

void led_status_install_begin(void)
{
    g_s_installing = true;
}

void led_status_install_end(void)
{
    g_s_installing = false;
}

void led_status_tick_install(void)
{
    if (!g_s_installing)
    {
        return; /* не установка (revert/recovery-стирание) — ничего не трогаем */
    }
    draw_heartbeat();
    bsp_led_set(LED_APP, phase_on(LED_INSTALL_PERIOD_MS, LED_INSTALL_ON_MS));
}

void led_status_flash_image_rejected(void)
{
    for (uint32_t i = 0U; i < LED_REJECT_COUNT; i++)
    {
        bsp_wdog_refresh(); /* ~640 мс блокирующей вспышки — держим watchdog сытым */
        bsp_led_on(LED_APP);
        bsp_delay(LED_REJECT_ON_MS);
        bsp_led_off(LED_APP);
        bsp_delay(LED_REJECT_ON_MS);
    }
}

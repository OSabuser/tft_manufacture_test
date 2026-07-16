/**
 * @file  main.c
 * @brief Заглушка tft_app для аппаратной верификации bootutil (Фаза 2) и
 *        recovery-логики (Фаза 6).
 *
 * tft_app ещё не реализована — bootloader'у некуда прыгать. Этот образ —
 * минимальный, но настоящий imgtool-подписанный XIP-образ с корректным
 * vector table по адресу слота: базово мигает LED_APP с периодом,
 * зависящим от STUB_BLINK_MS (задаётся компилятору), чтобы по частоте
 * мигания визуально отличить, какой слот реально выбрал bootloader. См.
 * firmware/bootloader/PLAN.md, "Аппаратная верификация Фазы 2".
 *
 * Фаза 6 добавила три независимые, настраиваемые компилятором оси —
 * стенд-контракт tft_app (см. PLAN.md, 6c) минимально, но по-настоящему:
 *   - STUB_CONFIRM_MODE — 0 сразу / 1 отложенно (STUB_CONFIRM_DELAY_MS) /
 *     2 никогда. Подтверждение — boot_set_next(fap, true, true) на
 *     СОБСТВЕННОМ слоте (STUB_OWN_SLOT_ID), НЕ boot_set_confirmed() — та
 *     жёстко пишет в FLASH_AREA_IMAGE_PRIMARY (Slot A) независимо от того,
 *     откуда реально исполняется код: для стаба в Slot Б это подтвердило бы
 *     чужой слот, а не себя.
 *   - STUB_HANG_MODE — 0 никогда / 1 до health-mark (сразу на входе) /
 *     2 после health-mark, до confirm / 3 после confirm (через
 *     STUB_HANG_DELAY_MS после факта подтверждения — чтобы успеть увидеть
 *     мигание глазами перед тем как оно застынет).
 *   - STUB_FEED_WDOG — 1 (дефолт, как в проде) / 0 — не кормить watchdog,
 *     детерминированно проверить сам механизм сброса.
 *
 * Без USB/CDC — визуальной индикации (частота/застывание LED_APP) достаточно
 * для чек-листа Фазы 6, минимальный код.
 *
 * Watchdog: загрузчик взводит аппаратный WDOG перед прыжком сюда, а WDE —
 * write-once (выключить нельзя). Поэтому заглушка, если сконфигурирована
 * его кормить (STUB_FEED_WDOG=1, дефолт), обязана делать это в главном цикле
 * — иначе WDOG сбросит плату через таймаут (см. bsp/wdog/README.md).
 */
#include "board.h"
#include "bootutil/bootutil_public.h"
#include "bsp/boot_state.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/wdog.h"
#include "flash_map.h"

#include <stdbool.h>

#ifndef STUB_BLINK_MS
#error "STUB_BLINK_MS must be defined (see firmware/bootloader/test_stub/CMakeLists.txt)"
#endif

#ifndef STUB_OWN_SLOT_ID
#error "STUB_OWN_SLOT_ID must be defined (0 = Slot A, 1 = Slot Б)"
#endif

/* Confirm mode: 0 = сразу, 1 = отложенно, 2 = никогда. */
#ifndef STUB_CONFIRM_MODE
#define STUB_CONFIRM_MODE 0
#endif

#ifndef STUB_CONFIRM_DELAY_MS
#define STUB_CONFIRM_DELAY_MS 3000U
#endif

/* Hang mode: 0 = никогда, 1 = до health-mark, 2 = после health-mark (до
 * confirm), 3 = после confirm. */
#ifndef STUB_HANG_MODE
#define STUB_HANG_MODE 0
#endif

#ifndef STUB_HANG_DELAY_MS
#define STUB_HANG_DELAY_MS 3000U
#endif

#ifndef STUB_FEED_WDOG
#define STUB_FEED_WDOG 1
#endif

/**
 * @brief Подтвердить СОБСТВЕННЫЙ слот (STUB_OWN_SLOT_ID).
 *
 * boot_set_next(fap, active=true, confirm=true) — не boot_set_confirmed():
 * та жёстко работает с FLASH_AREA_IMAGE_PRIMARY (Slot A) вне зависимости от
 * того, какой слот реально исполняется; для Direct-XIP с двумя равноправными
 * слотами это подтвердило бы не тот слот при исполнении из Slot Б.
 */
static void confirm_self(void)
{
    const struct flash_area *p_fap;
    if (flash_area_open((uint8_t) STUB_OWN_SLOT_ID, &p_fap) == 0)
    {
        (void) boot_set_next(p_fap, true, true);
        flash_area_close(p_fap);
    }
}

int main(void)
{
    board_hw_init();
    bsp_led_init();
    bsp_tick_init();

#if STUB_HANG_MODE == 1
    for (;;) { } /* до health-mark — Класс A: свежий образ виснет сразу */
#endif

    /* health-mark == bsp_boot_attempt_reset() (см. bsp/boot_state.h) — вызывать
     * его безусловно перед потенциальным зависанием НЕЛЬЗЯ: он обнулял бы
     * счётчик попыток на КАЖДОМ цикле ДО того, как зависание успевает
     * засчитаться, и recovery-фолбэк/recovery-режим не сработали бы никогда
     * (найдено на железе — HANG_MODE=3 резетился бесконечно вместо остановки
     * на пороге). Это тот же класс "честной границы", что уже описан в
     * PLAN.md (образ обнуляет счётчик, ПОТОМ виснет — таймером не отличить
     * "здоров" от "здоров, но детерминированно виснет"): здесь HANG_MODE
     * снимает эту неоднозначность на этапе компиляции — если конфигурация
     * гарантированно виснет (2 — после health-mark, до confirm; 3 — после
     * confirm), health-mark не зовём вообще, счётчик копится корректно.
     * Здоровый стаб (HANG_MODE 0) и "виснет до health-mark" (1, сюда и не
     * доходит) — не затронуты. */
#if (STUB_HANG_MODE != 2) && (STUB_HANG_MODE != 3)
    bsp_boot_health_mark(); /* "дошёл до устойчивого состояния" (Фаза 6, 6c) */
#endif

#if STUB_HANG_MODE == 2
    for (;;) { } /* после (несостоявшегося) health-mark, до confirm */
#endif

    bool confirmed = false;

#if STUB_CONFIRM_MODE == 0
    confirm_self();
    confirmed = true;
#endif

    uint32_t start_ms = bsp_tick_get_ms();
    (void) start_ms; /* не используется, если ни один из режимов ниже её не читает */

    while (1)
    {
#if STUB_FEED_WDOG
        bsp_wdog_refresh(); /* обслуживаем унаследованный от загрузчика WDOG */
#endif

#if STUB_CONFIRM_MODE == 1
        if (!confirmed && ((bsp_tick_get_ms() - start_ms) >= STUB_CONFIRM_DELAY_MS))
        {
            confirm_self();
            confirmed = true;
        }
#endif

#if STUB_HANG_MODE == 3
        if (confirmed &&
            ((bsp_tick_get_ms() - start_ms) >= (STUB_CONFIRM_DELAY_MS + STUB_HANG_DELAY_MS)))
        {
            for (;;) { } /* после confirm — Класс B: подтверждённый образ виснет в рантайме */
        }
#endif

        bsp_led_toggle(LED_APP);
        bsp_delay(STUB_BLINK_MS);
    }
}

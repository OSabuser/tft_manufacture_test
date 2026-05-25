/**
 * @file  test_display.c
 * @brief Тест-модуль firmware_test: TFT-дисплей (ELCDIF RGB888).
 *
 * Два этапа:
 *
 *   Этап 1 — Цвет (все дисплеи, ~1 мин)
 *     4 шага: Red → Green → Blue → White.
 *     Каждый шаг: заливка сплошным цветом → ожидание FRAME_DONE
 *     → confirm оператора (таймаут 15 с).
 *
 *   Этап 2 — Ротация (только TFT7/TFT8/TFT10, ~30 с)
 *     Диагностирует непропаянные LR/UD пины.
 *     Паттерн: левая половина RED, правая BLUE.
 *     ROTATE_0 confirm → ROTATE_90 confirm → восстановить ROTATE_0.
 *
 * Тип дисплея определяется через DISPLAY_TEST_TYPE:
 *   — сейчас: жёсткий define (BSP_DISPLAY_TFT8) через CMake.
 *   — TODO: читать из Flash-конфига (Вариант C, см. PLAN.md Этап 5).
 *
 * Фреймбуфер — статический, в NonCacheable SDRAM (секция NonCacheable).
 * Размер по максимальному дисплею: BSP_DISPLAY_MAX_HEIGHT × BSP_DISPLAY_MAX_WIDTH.
 *
 * Frame sync: volatile bool g_s_frame_done — устанавливается в ISR callback,
 * сбрасывается перед каждым bsp_display_set_next_buffer().
 * USB CDC поллится в цикле ожидания — стек остаётся живым.
 */

#include "bsp/display.h"
#include "bsp/usb_cdc.h"
#include "fsl_common.h"
#include "test_module.h"
#include "test_runner.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* ── Тип дисплея ─────────────────────────────────────────────────────── */

/*
 * TODO (Вариант C): заменить на чтение из Flash-конфига платы.
 * До реализации конфига тип задаётся через CMake:
 *   target_compile_definitions(firmware_test PRIVATE
 *       DISPLAY_TEST_TYPE=BSP_DISPLAY_TFT8)
 */
#ifndef DISPLAY_TEST_TYPE
#define DISPLAY_TEST_TYPE BSP_DISPLAY_TFT8
#endif

/* ── Вспомогательные функции ────────────────────────────────────────────── */

static test_result_t make_fail(const char *p_detail)
{
    test_result_t result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", p_detail);
    return result;
}

/* ── Константы ───────────────────────────────────────────────────────── */

/** @brief XRGB8888 цвета для тестовых заливок. */
#define DISPLAY_COLOR_RED   0x00FF0000UL
#define DISPLAY_COLOR_GREEN 0x0000FF00UL
#define DISPLAY_COLOR_BLUE  0x000000FFUL
#define DISPLAY_COLOR_WHITE 0x00FFFFFFUL

/** @brief Таймаут confirm для каждого шага, мс. */
#define DISPLAY_CONFIRM_TIMEOUT_MS 15000U

/* ── Фреймбуфер в NonCacheable SDRAM ────────────────────────────────── */

/** @brief Один кадр максимального разрешения, XRGB8888. */
static AT_NONCACHEABLE_SECTION_ALIGN(
    uint32_t g_s_framebuf[BSP_DISPLAY_MAX_HEIGHT * BSP_DISPLAY_MAX_WIDTH], 64U);

/* ── Состояние модуля ────────────────────────────────────────────────── */

static bool g_s_ready;
static volatile bool g_s_frame_done;

/* ── Frame-done callback ─────────────────────────────────────────────── */

/** @brief Вызывается из LCDIF ISR. ISR-safe: только запись в volatile. */
static void display_frame_cb(void)
{
    g_s_frame_done = true;
}

/* ── Вспомогательные функции ─────────────────────────────────────────── */

/**
 * @brief Залить весь буфер одним цветом XRGB8888.
 *
 * Поллит USB CDC каждые 16 KB для поддержания USB-стека.
 */
static void fill_solid(uint32_t color)
{
    const bsp_display_size_t *p_sz = bsp_display_get_size();
    const uint32_t total           = (uint32_t) p_sz->width * (uint32_t) p_sz->height;

    for (uint32_t i = 0U; i < total; i++)
    {
        if ((i & 0x3FFFU) == 0U)
        {
            bsp_usb_cdc_poll();
        }
        g_s_framebuf[i] = color;
    }
}

/**
 * @brief Залить левую половину color_l, правую — color_r.
 *
 * Используется в тесте ротации для детектирования непропаянных LR/UD пинов.
 */
static void fill_half(uint32_t color_l, uint32_t color_r)
{
    const bsp_display_size_t *p_sz = bsp_display_get_size();
    const uint16_t HALF            = p_sz->width / 2U;

    for (uint16_t row = 0U; row < p_sz->height; row++)
    {
        const uint32_t BASE = (uint32_t) row * (uint32_t) p_sz->width;
        for (uint16_t col = 0U; col < p_sz->width; col++)
        {
            g_s_framebuf[BASE + col] = (col < HALF) ? color_l : color_r;
        }
        if ((row & 0x1FU) == 0U)
        {
            bsp_usb_cdc_poll();
        }
    }
}

/**
 * @brief Отправить буфер в ELCDIF и заблокироваться до завершения кадра.
 *
 * Поллит USB CDC в цикле ожидания.
 */
static void wait_frame(void)
{
    g_s_frame_done = false;
    bsp_display_set_next_buffer((uint32_t) g_s_framebuf);
    while (!g_s_frame_done)
    {
        bsp_usb_cdc_poll();
    }
}

/* ── Шаги теста ──────────────────────────────────────────────────────── */

/**
 * @brief Залить цвет, отобразить, запросить подтверждение оператора.
 *
 * @param color    XRGB8888 цвет заливки.
 * @param p_id     ID confirm_request.
 * @param p_prompt Инструкция оператору.
 * @param p_out    Заполняется при FAIL.
 * @return true при подтверждении оператором.
 */
static bool step_color(uint32_t color, const char *p_id, const char *p_prompt, test_result_t *p_out)
{
    fill_solid(color);
    wait_frame();

    const confirm_params_t params = {
        .id         = p_id,
        .prompt     = p_prompt,
        .timeout_ms = DISPLAY_CONFIRM_TIMEOUT_MS,
    };

    if (!test_runner_wait_confirm(&params))
    {
        (void) snprintf(p_out->detail, TEST_DETAIL_SIZE, "%s not confirmed", p_id);
        p_out->status = TEST_STATUS_FAIL;
        return false;
    }
    return true;
}

/**
 * @brief Применить ротацию, залить паттерн, запросить confirm.
 *
 * Вспомогательная функция для step_rotation() — держит её в лимите.
 *
 * @param rotation  Ориентация для установки.
 * @param p_id      ID confirm_request.
 * @param p_prompt  Инструкция оператору.
 * @param p_out     Заполняется при FAIL.
 * @return true при подтверждении.
 */
static bool step_rot_apply_and_confirm(bsp_display_rotation_t rotation, const char *p_id,
                                       const char *p_prompt, test_result_t *p_out)
{
    (void) bsp_display_set_rotation(rotation);
    fill_half(DISPLAY_COLOR_RED, DISPLAY_COLOR_BLUE);
    wait_frame();

    const confirm_params_t params = {
        .id         = p_id,
        .prompt     = p_prompt,
        .timeout_ms = DISPLAY_CONFIRM_TIMEOUT_MS,
    };

    if (!test_runner_wait_confirm(&params))
    {
        (void) snprintf(p_out->detail, TEST_DETAIL_SIZE, "%s not confirmed", p_id);
        p_out->status = TEST_STATUS_FAIL;
        return false;
    }
    return true;
}

/**
 * @brief Этап 2: тест ротации — диагностика непропаянных LR/UD пинов.
 *
 * ROTATE_0: левая зона RED, правая BLUE → confirm.
 * ROTATE_90: те же данные в буфере, аппаратный флип → confirm.
 * Восстанавливает ROTATE_0 независимо от результата.
 */
static bool step_rotation(test_result_t *p_out)
{
    bool ok = step_rot_apply_and_confirm(BSP_DISPLAY_ROTATE_0, "display_rot0",
                                         "Screen: left RED, right BLUE?", p_out);
    if (ok)
    {
        ok = step_rot_apply_and_confirm(BSP_DISPLAY_FLIP_HORIZONTAL, "display_rot_base",
                                        "Left RED and right BLUE swapped sides?", p_out);
    }

    /* Восстановить ROTATE_0 в любом исходе */
    (void) bsp_display_set_rotation(BSP_DISPLAY_ROTATE_0);
    return ok;
}

/* ── Реализация тест-модуля ───────────────────────────────────────────── */

static void display_test_init(void)
{
    g_s_ready      = false;
    g_s_frame_done = false;

    bsp_status_t status = bsp_display_init((bsp_display_type_t) DISPLAY_TEST_TYPE,
                                           (uint32_t) g_s_framebuf, display_frame_cb);
    if (status != BSP_OK)
    {
        return;
    }

    /* Явная установка ROTATE_0 — детерминированное начальное состояние */
    (void) bsp_display_set_rotation(BSP_DISPLAY_ROTATE_0);
    g_s_ready = true;
}

static test_result_t display_test_run(void)
{

    if (!g_s_ready)
    {
        return make_fail("display init failed");
    }

    test_result_t fail_result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U, .detail = { 0 } };

    /* Этап 1: цвет */
    if (!step_color(DISPLAY_COLOR_RED, "display_red", "Screen is solid red?", &fail_result))
    {
        return fail_result;
    }
    if (!step_color(DISPLAY_COLOR_GREEN, "display_green", "Screen is solid green?", &fail_result))
    {
        return fail_result;
    }
    if (!step_color(DISPLAY_COLOR_BLUE, "display_blue", "Screen is solid blue?", &fail_result))
    {
        return fail_result;
    }
    if (!step_color(DISPLAY_COLOR_WHITE, "display_white", "Screen is solid white?", &fail_result))
    {
        return fail_result;
    }

    /* Этап 2: ротация — только для дисплеев с ножками LR/UD */
    if (bsp_display_get_type() != BSP_DISPLAY_TFT4)
    {
        if (!step_rotation(&fail_result))
        {
            return fail_result;
        }
    }

    return (test_result_t){
        .status      = TEST_STATUS_PASS,
        .duration_ms = 0U,
        .detail      = { 0 },
    };
}

static void display_test_deinit(void)
{
    (void) bsp_display_deinit();
    g_s_ready      = false;
    g_s_frame_done = false;
}

/* ── Дескриптор модуля ────────────────────────────────────────────────── */

const test_module_t K_TEST_DISPLAY = {
    .id                 = "display",
    .name               = "TFT Display RGB888",
    .critical           = false,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = display_test_init,
    .run                = display_test_run,
    .deinit             = display_test_deinit,
};
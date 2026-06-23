/**
 * @file  test_buttons.c
 * @brief Тест-модуль firmware_test: тактовые кнопки Test_But_1 / Test_But_2.
 *
 * Логика теста:
 *   1. Отправить confirm_request("btn1_press") как инструкцию оператору.
 *   2. Поллить bsp_button — ждать события нажатия Test_But_1,
 *      таймаут BTN_PRESS_TIMEOUT_MS → SKIP.
 *   3. То же для Test_But_2 ("btn2_press").
 *
 * Хост НЕ отправляет {"type":"confirm",...}.
 * confirm_request — только UI-подсказка оператору.
 * Физическое событие детектируется через bsp_button_get_event_pressed().
 *
 * Аппаратура:
 *   Test_But_1 — GPIO_B1_14 / GPIO2[30], pull-up 3V3, нажатие = LOW
 *   Test_But_2 — GPIO_B1_15 / GPIO2[31], pull-up 3V3, нажатие = LOW
 */

#include "bsp/button.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "cli.h"
#include "protocol.h"
#include "test_module.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Таймаут ожидания нажатия кнопки, мс. */
#define BTN_PRESS_TIMEOUT_MS 10000U

/** @brief Период вызова bsp_button_poll(), мс. */
#define BTN_POLL_PERIOD_MS 5U

/* ── Вспомогательные функции ────────────────────────────────────────────── */

/**
 * @brief Создать результат FAIL с текстовым описанием.
 *
 * @param p_detail  Строка описания (копируется в result.detail).
 * @return Заполненный test_result_t со статусом TEST_STATUS_FAIL.
 */
static test_result_t make_fail(const char *p_detail)
{
    test_result_t result = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", p_detail);
    return result;
}

/**
 * @brief Создать результат SKIP с текстовым описанием.
 *
 * @param p_detail  Строка описания (копируется в result.detail).
 * @return Заполненный test_result_t со статусом TEST_STATUS_SKIP.
 */
static test_result_t make_skip(const char *p_detail)
{
    test_result_t result = { .status = TEST_STATUS_SKIP, .duration_ms = 0U };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", p_detail);
    return result;
}

/**
 * @brief Ждать нажатия кнопки с debounce-поллингом.
 *
 * Отправляет confirm_request как инструкцию оператору — хост не отвечает
 * JSON-confirm. Физическое нажатие детектируется через bsp_button.
 * USB CDC поллится в каждой итерации; bsp_button_poll() вызывается
 * каждые BTN_POLL_PERIOD_MS мс через rate-limiting по bsp_tick_get_ms().
 *
 * @param btn        Кнопка для ожидания.
 * @param p_id       Идентификатор confirm_request.
 * @param p_prompt   Инструкция оператору.
 * @param timeout_ms Таймаут ожидания, мс.
 * @return true если нажатие зафиксировано до таймаута, false при таймауте.
 */
static bool wait_button_press(bsp_button_t btn, const char *p_id, const char *p_prompt,
                              uint32_t timeout_ms)
{
    const confirm_params_t params = {
        .id         = p_id,
        .prompt     = p_prompt,
        .timeout_ms = timeout_ms,
    };
    protocol_send_confirm_request(&params);

    const uint32_t DEADLINE = bsp_tick_get_ms() + timeout_ms;
    uint32_t next_poll_ms   = bsp_tick_get_ms();

    while (bsp_tick_get_ms() < DEADLINE)
    {
        bsp_usb_cdc_poll();
        cli_process();

        if (bsp_tick_get_ms() >= next_poll_ms)
        {
            bsp_button_poll();
            next_poll_ms += BTN_POLL_PERIOD_MS;

            /* Дренировать события обеих кнопок сразу после poll.
             * Нажатие не-целевой кнопки отбрасывается здесь же —
             * иначе stale-событие засчитается в следующем вызове. */
            bool ev1 = bsp_button_get_event_pressed(BSP_BUTTON_1);
            bool ev2 = bsp_button_get_event_pressed(BSP_BUTTON_2);

            if ((btn == BSP_BUTTON_1 && ev1) || (btn == BSP_BUTTON_2 && ev2))
            {
                return true;
            }
        }
    }

    return false;
}

/* ── Реализация тест-модуля ─────────────────────────────────────────────── */

/**
 * @brief Инициализация: сброс debounce-состояния перед тестом.
 *
 * GPIO уже настроен в BOARD_InitPins(). bsp_button_init() только
 * сбрасывает счётчики и флаги событий — не трогает железо.
 * Гарантирует, что удержание кнопки до старта теста не даёт
 * ложного события: get_event_pressed() срабатывает только на переход.
 */
static void buttons_test_init(void)
{
    bsp_button_init();
}

/**
 * @brief Выполнить тест кнопок.
 *
 * Шаг 1: ждать нажатия Test_But_1.
 * Шаг 2: ждать нажатия Test_But_2.
 * Таймаут любого шага → SKIP.
 *
 * @return test_result_t с итогом теста.
 */
static test_result_t buttons_test_run(void)
{
    if (!wait_button_press(BSP_BUTTON_1, "btn1_press", "Press Test_But_1", BTN_PRESS_TIMEOUT_MS))
    {
        return make_skip("btn1_press timeout");
    }

    if (!wait_button_press(BSP_BUTTON_2, "btn2_press", "Press Test_But_2", BTN_PRESS_TIMEOUT_MS))
    {
        return make_skip("btn2_press timeout");
    }

    return (test_result_t){
        .status      = TEST_STATUS_PASS,
        .duration_ms = 0U,
        .detail      = { 0 },
    };
}

/* ── Дескриптор модуля ──────────────────────────────────────────────────── */

/** @brief Дескриптор тест-модуля кнопок для реестра test_runner. */
const test_module_t K_TEST_BUTTONS = {
    .id                 = "buttons",
    .name               = "Test Buttons",
    .critical           = false,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = buttons_test_init,
    .run                = buttons_test_run,
    .deinit             = NULL,
};
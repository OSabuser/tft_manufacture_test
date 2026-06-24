/*
 * test_opto — HIL-тест оптоизолированных входов (PS2801-4)
 *
 * Три канала: EXT_IN1 (RLY3), EXT_IN2 (RLY4), RS_RX (RLY2).
 * Все три — MODE_LEVEL, rs_as_gpio = true.
 *
 * 6 шагов: confirm_request → M5 переключает реле → settle → bsp_opto_read().
 * Верификация синхронная: M5 переключил реле до отправки confirmed:true.
 */
#include "bsp/opto.h"
#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "protocol.h"
#include "test_module.h"
#include "test_runner.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* ── Константы ───────────────────────────────────────────────────────────── */

/** Период дебаунса оптовходов, мс. */
#define OPTO_DEBOUNCE_MS 10U

/**
 * Пауза после confirm перед bsp_opto_read(), мс.
 * Должна перекрывать OPTO_DEBOUNCE_MS с запасом на jitter реле.
 * M5 уже переключил реле до отправки confirmed:true.
 */
#define OPTO_SETTLE_MS 30U

/* ── Описание одного шага ────────────────────────────────────────────────── */

typedef struct
{
    const char *p_confirm_id;        /**< id для confirm_request / confirm. */
    const char *p_prompt;            /**< Инструкция для TUI / M5.          */
    bsp_opto_ch_t channel;           /**< Канал для верификации.             */
    bsp_opto_state_t expected_state; /**< Ожидаемое состояние.              */
} opto_step_t;

/* ── Таблица шагов (PLAN.md §6б) ─────────────────────────────────────────── */

static const opto_step_t K_STEPS[] = {
    {
        .p_confirm_id   = "opto_in1_active",
        .p_prompt       = "M5: RLY3 ON -> IN1 ACTIVE",
        .channel        = BSP_OPTO_CH_IN1,
        .expected_state = BSP_OPTO_STATE_ACTIVE,
    },
    {
        .p_confirm_id   = "opto_in1_inactive",
        .p_prompt       = "M5: RLY3 OFF -> IN1 INACTIVE",
        .channel        = BSP_OPTO_CH_IN1,
        .expected_state = BSP_OPTO_STATE_INACTIVE,
    },
    {
        .p_confirm_id   = "opto_in2_active",
        .p_prompt       = "M5: RLY4 ON -> IN2 ACTIVE",
        .channel        = BSP_OPTO_CH_IN2,
        .expected_state = BSP_OPTO_STATE_ACTIVE,
    },
    {
        .p_confirm_id   = "opto_in2_inactive",
        .p_prompt       = "M5: RLY4 OFF -> IN2 INACTIVE",
        .channel        = BSP_OPTO_CH_IN2,
        .expected_state = BSP_OPTO_STATE_INACTIVE,
    },
    {
        .p_confirm_id   = "opto_rs_active",
        .p_prompt       = "M5: RLY2 ON -> RS ACTIVE",
        .channel        = BSP_OPTO_CH_RS,
        .expected_state = BSP_OPTO_STATE_ACTIVE,
    },
    {
        .p_confirm_id   = "opto_rs_inactive",
        .p_prompt       = "M5: RLY2 OFF -> RS INACTIVE",
        .channel        = BSP_OPTO_CH_RS,
        .expected_state = BSP_OPTO_STATE_INACTIVE,
    },
};

#define OPTO_STEP_COUNT ((size_t) (sizeof(K_STEPS) / sizeof(K_STEPS[0])))

/* ── Реализация тест-модуля ──────────────────────────────────────────────── */

/**
 * @brief Инициализация: все три канала MODE_LEVEL, rs_as_gpio = true.
 *
 * Коллбэки не нужны — состояние читается через bsp_opto_read() синхронно
 * после confirm.
 */
static void opto_init(void)
{
    static const bsp_opto_config_t K_CFG = {
        .callbacks   = { NULL, NULL, NULL },
        .modes       = { BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL, BSP_OPTO_MODE_LEVEL },
        .edges       = { BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING, BSP_OPTO_EDGE_RISING },
        .rs_as_gpio  = true,
        .debounce_ms = OPTO_DEBOUNCE_MS,
    };

    (void) bsp_opto_init(&K_CFG);
}

/**
 * @brief Выполнение теста: 6 шагов confirm_request → settle → verify.
 *
 * Для каждого шага:
 *   1. Отправить confirm_request — TUI командует M5 переключить реле.
 *   2. Ждать confirm через test_runner_wait_confirm().
 *      Возвращает false при таймауте или отказе оператора.
 *   3. После confirm дать дебаунсу отработать (OPTO_SETTLE_MS),
 *      гоняя bsp_opto_process() + bsp_usb_cdc_poll().
 *   4. Прочитать bsp_opto_read() и сравнить с expected.
 *
 * @return test_result_t с полями status и detail.
 */
static test_result_t opto_run(void)
{
    test_result_t result = {
        .status      = TEST_STATUS_PASS,
        .duration_ms = 0U,
        .detail      = "",
    };

    for (size_t i = 0U; i < OPTO_STEP_COUNT; i++)
    {
        const opto_step_t *p_step = &K_STEPS[i];

        /* 1–2. Запросить действие M5 и ждать confirm */
        const confirm_params_t params = {
            .id         = p_step->p_confirm_id,
            .prompt     = p_step->p_prompt,
            .timeout_ms = 0U, /* использовать PROTOCOL_CONFIRM_TIMEOUT_MS */
        };

        bool confirmed = test_runner_wait_confirm(&params);

        if (!confirmed)
        {
            /*
             * Таймаут или явный отказ — тест пропускается целиком.
             * Одна из двух причин: "confirm timeout" или "operator declined".
             * test_runner_wait_confirm() не разделяет их — используем "confirm timeout"
             * как универсальный повод для SKIP.
             */
            result.status = TEST_STATUS_SKIP;
            (void) snprintf(result.detail, TEST_DETAIL_SIZE, "confirm timeout on %s",
                            p_step->p_confirm_id);
            return result;
        }

        /* 3. Settle: дать дебаунсу отработать после переключения реле */
        uint32_t settle_until = bsp_tick_get_ms() + OPTO_SETTLE_MS;
        while (bsp_tick_get_ms() < settle_until)
        {
            bsp_opto_process();
            bsp_usb_cdc_poll();
        }

        /* 4. Верификация */
        bsp_opto_state_t got = bsp_opto_read(p_step->channel);
        if (got != p_step->expected_state)
        {
            const char *p_exp_str =
                (p_step->expected_state == BSP_OPTO_STATE_ACTIVE) ? "ACTIVE" : "INACTIVE";
            const char *p_got_str = (got == BSP_OPTO_STATE_ACTIVE) ? "ACTIVE" : "INACTIVE";

            result.status = TEST_STATUS_FAIL;
            (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s mismatch: expected %s got %s",
                            p_step->p_confirm_id, p_exp_str, p_got_str);
            return result;
        }
    }

    return result;
}

/* ── Дескриптор тест-модуля ──────────────────────────────────────────────── */

/** @brief Дескриптор для регистрации в test_runner. */
const test_module_t K_TEST_OPTO = {
    .id                 = "opto",
    .name               = "Opto Inputs",
    .critical           = false,
    .requires_hil       = true,
    .pre_confirm_prompt = NULL,
    .init               = opto_init,
    .run                = opto_run,
    .deinit             = NULL,
};
/**
 * @file  test_runner.c
 * @brief Реализация реестра тест-модулей и state machine запуска.
 *
 * State machine:
 *   IDLE         — ожидание команды от хоста.
 *   PRE_CONFIRM  — отправлен confirm_request до запуска теста (pre_confirm_prompt),
 *                  ожидание JSON-ответа оператора или таймаута.
 *   RUNNING      — тест выполняется (dispatch_test блокирует).
 *                  Защищает от ложного is_busy()==false во время
 *                  внутреннего polling loop в test_runner_wait_confirm().
 */

#include "test_runner.h"

#include "bsp/tick.h"
#include "bsp/usb_cdc.h"
#include "cli.h"
#include "protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* ── Реестр тестов ─────────────────────────────────────────────────────────*/
#ifndef UNIT_TEST
extern const test_module_t K_TEST_SDRAM;
extern const test_module_t K_TEST_QSPI;
extern const test_module_t K_TEST_USD;
extern const test_module_t K_TEST_DISPLAY;
extern const test_module_t K_TEST_BUTTONS;

static const test_module_t *const k_registry[] = {

    &K_TEST_SDRAM, &K_TEST_QSPI, &K_TEST_USD, &K_TEST_DISPLAY, &K_TEST_BUTTONS,
};

#define REGISTRY_SIZE (sizeof(k_registry) / sizeof(k_registry[0]))

#else /* UNIT_TEST — реестр предоставляется тест-файлом */

extern const test_module_t *g_unit_test_registry[];
extern size_t g_unit_test_registry_size;

/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
#define k_registry    g_unit_test_registry
/* NOLINTNEXTLINE(cppcoreguidelines-macro-usage) */
#define REGISTRY_SIZE g_unit_test_registry_size

#endif /* UNIT_TEST */

/* ── Константы ─────────────────────────────────────────────────────────── */

#define RUNNER_CONFIRM_ID_SIZE 32U

/* ── Типы ──────────────────────────────────────────────────────────────── */

typedef enum runner_state_e
{
    RUNNER_STATE_IDLE,
    RUNNER_STATE_PRE_CONFIRM,
    RUNNER_STATE_RUNNING,
} runner_state_t;

typedef enum runner_mode_e
{
    RUNNER_MODE_SINGLE,
    RUNNER_MODE_ALL,
} runner_mode_t;

/* ── Статическое состояние ─────────────────────────────────────────────── */

static runner_state_t g_s_state;
static runner_mode_t g_s_mode;
static size_t g_s_current_idx;

static volatile bool g_s_confirm_received;
static volatile bool g_s_confirm_value;
static char g_s_pending_confirm_id[RUNNER_CONFIRM_ID_SIZE];
static uint32_t g_s_confirm_deadline_ms;

/* Счётчики итога (используются только в RUNNER_MODE_ALL) */
static uint8_t g_s_passed;
static uint8_t g_s_failed;
static uint8_t g_s_skipped;
static bool g_s_critical_failed;

/* ── Forward declaration ───────────────────────────────────────────────── */

static void start_test_at(size_t idx);

/* ── Внутренние вспомогательные функции ────────────────────────────────── */

/**
 * @brief Найти индекс теста по id. Возвращает REGISTRY_SIZE если не найден.
 */
static size_t find_test_by_id(const char *p_id)
{
    for (size_t i = 0U; i < REGISTRY_SIZE; i++)
    {
        if (strcmp(k_registry[i]->id, p_id) == 0)
        {
            return i;
        }
    }
    return REGISTRY_SIZE;
}

/**
 * @brief Создать результат SKIP с текстовой причиной.
 */
static test_result_t make_skip_result(const char *p_reason)
{
    test_result_t result = {
        .status      = TEST_STATUS_SKIP,
        .duration_ms = 0U,
    };
    (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", p_reason);
    return result;
}

/**
 * @brief Обновить счётчики pass/fail/skip и флаг critical_failed.
 */
static void update_counters(const test_result_t *p_result, bool critical)
{
    switch (p_result->status)
    {
    case TEST_STATUS_PASS:
        g_s_passed++;
        break;
    case TEST_STATUS_FAIL:
        g_s_failed++;
        if (critical)
        {
            g_s_critical_failed = true;
        }
        break;
    case TEST_STATUS_SKIP:
        g_s_skipped++;
        break;
    default:
        break;
    }
}

/**
 * @brief Взвести механизм ожидания confirm (id + deadline).
 */
static void arm_confirm(const char *p_id, uint32_t timeout_ms)
{
    (void) snprintf(g_s_pending_confirm_id, sizeof(g_s_pending_confirm_id), "%s", p_id);
    g_s_confirm_received    = false;
    g_s_confirm_value       = false;
    g_s_confirm_deadline_ms = bsp_tick_get_ms() + timeout_ms;
}

/**
 * @brief Выполнить тест: init → run → deinit. Замерить время.
 */
static test_result_t execute_test(const test_module_t *p_mod)
{
    const uint32_t START_MS = bsp_tick_get_ms();

    if (p_mod->init != NULL)
    {
        p_mod->init();
    }

    test_result_t result = p_mod->run();

    if (p_mod->deinit != NULL)
    {
        p_mod->deinit();
    }

    result.duration_ms = bsp_tick_get_ms() - START_MS;
    return result;
}

/**
 * @brief Выслать test_begin, выполнить тест, обновить счётчики, выслать test_result.
 */
static void dispatch_test(const test_module_t *p_mod)
{
    protocol_send_test_begin(p_mod);
    test_result_t result = execute_test(p_mod);
    update_counters(&result, p_mod->critical);
    protocol_send_test_result(p_mod, &result);
}

/**
 * @brief Отправить SKIP для всех тестов начиная с from_idx и обновить счётчик.
 *
 * Вызывается при critical fail в run_all — оставшиеся тесты скипаются.
 */
static void skip_from(size_t from_idx)
{
    for (size_t i = from_idx; i < REGISTRY_SIZE; i++)
    {
        const test_module_t *mod = k_registry[i];
        test_result_t result     = make_skip_result("critical test failed");
        protocol_send_test_begin(mod);
        protocol_send_test_result(mod, &result);
        g_s_skipped++;
    }
    g_s_current_idx = REGISTRY_SIZE;
}

/**
 * @brief Продвинуть runner после завершения текущего теста.
 *
 * В режиме SINGLE — переходит в IDLE.
 * В режиме ALL — запускает следующий тест или отправляет summary.
 */
static void advance_after_current(void)
{
    if (g_s_mode == RUNNER_MODE_SINGLE)
    {
        g_s_state = RUNNER_STATE_IDLE;
        return;
    }

    g_s_current_idx++;

    if (g_s_current_idx >= REGISTRY_SIZE)
    {
        protocol_send_summary(g_s_passed, g_s_failed, g_s_skipped, !g_s_critical_failed);
        g_s_state = RUNNER_STATE_IDLE;
        return;
    }

    start_test_at(g_s_current_idx);
}

/**
 * @brief Выставить RUNNING, выполнить текущий тест, вернуть IDLE, продвинуть runner.
 */
static void execute_and_advance(void)
{
    g_s_state = RUNNER_STATE_RUNNING;
    dispatch_test(k_registry[g_s_current_idx]);
    g_s_state = RUNNER_STATE_IDLE;
    advance_after_current();
}

/**
 * @brief Запустить тест по индексу: с pre_confirm или сразу.
 *
 * При critical_failed в режиме ALL — скипает все оставшиеся тесты и
 * отправляет summary.
 */
static void start_test_at(size_t idx)
{
    g_s_current_idx = idx;

    if (g_s_critical_failed)
    {
        skip_from(idx);
        protocol_send_summary(g_s_passed, g_s_failed, g_s_skipped, false);
        g_s_state = RUNNER_STATE_IDLE;
        return;
    }

    const test_module_t *mod = k_registry[idx];

    if (mod->pre_confirm_prompt != NULL)
    {
        confirm_params_t params = {
            .id         = mod->id,
            .prompt     = mod->pre_confirm_prompt,
            .timeout_ms = 0U,
        };
        protocol_send_confirm_request(&params);
        arm_confirm(mod->id, PROTOCOL_CONFIRM_TIMEOUT_MS);
        g_s_state = RUNNER_STATE_PRE_CONFIRM;
        return;
    }

    execute_and_advance();
}

/**
 * @brief Обработать состояние PRE_CONFIRM: проверить confirm или таймаут.
 */
static void process_pre_confirm(void)
{
    bool timed_out = (bsp_tick_get_ms() >= g_s_confirm_deadline_ms);

    if (!g_s_confirm_received && !timed_out)
    {
        return;
    }

    if (g_s_confirm_received && g_s_confirm_value)
    {
        execute_and_advance();
        return;
    }

    /* Таймаут или отказ оператора — скипаем тест */
    const test_module_t *mod = k_registry[g_s_current_idx];
    const char *p_reason     = g_s_confirm_received ? "operator declined" : "confirm timeout";
    test_result_t result     = make_skip_result(p_reason);

    g_s_state = RUNNER_STATE_IDLE;
    protocol_send_test_begin(mod);
    protocol_send_test_result(mod, &result);
    update_counters(&result, mod->critical);
    advance_after_current();
}

/* ── Public API ────────────────────────────────────────────────────────── */

void test_runner_init(void)
{
    g_s_state                 = RUNNER_STATE_IDLE;
    g_s_mode                  = RUNNER_MODE_SINGLE;
    g_s_current_idx           = 0U;
    g_s_confirm_received      = false;
    g_s_confirm_value         = false;
    g_s_confirm_deadline_ms   = 0U;
    g_s_passed                = 0U;
    g_s_failed                = 0U;
    g_s_skipped               = 0U;
    g_s_critical_failed       = false;
    g_s_pending_confirm_id[0] = '\0';
}

void test_runner_process(void)
{
    if (g_s_state == RUNNER_STATE_PRE_CONFIRM)
    {
        process_pre_confirm();
    }
}

void test_runner_run_single(const char *p_id)
{
    if (g_s_state != RUNNER_STATE_IDLE)
    {
        protocol_send_error("BUSY");
        return;
    }

    size_t idx = find_test_by_id(p_id);

    if (idx >= REGISTRY_SIZE)
    {
        protocol_send_error("UNKNOWN_TEST");
        return;
    }

    g_s_mode = RUNNER_MODE_SINGLE;
    start_test_at(idx);
}

void test_runner_run_all(void)
{
    if (g_s_state != RUNNER_STATE_IDLE)
    {
        protocol_send_error("BUSY");
        return;
    }

    if (REGISTRY_SIZE == 0U)
    {
        protocol_send_summary(0U, 0U, 0U, true);
        return;
    }

    g_s_passed          = 0U;
    g_s_failed          = 0U;
    g_s_skipped         = 0U;
    g_s_critical_failed = false;
    g_s_mode            = RUNNER_MODE_ALL;
    start_test_at(0U);
}

void test_runner_on_confirm(const char *p_id, bool confirmed)
{
    if (strcmp(p_id, g_s_pending_confirm_id) != 0)
    {
        return; /* stale или несовпадение id — игнорировать */
    }

    g_s_confirm_value    = confirmed;
    g_s_confirm_received = true;
}

bool test_runner_is_busy(void)
{
    return (g_s_state != RUNNER_STATE_IDLE);
}

bool test_runner_wait_confirm(const confirm_params_t *p_params)
{
    const uint32_t EFFECTIVE_TIMEOUT =
        (p_params->timeout_ms == 0U) ? PROTOCOL_CONFIRM_TIMEOUT_MS : p_params->timeout_ms;

    protocol_send_confirm_request(p_params);
    arm_confirm(p_params->id, EFFECTIVE_TIMEOUT);

    while (!g_s_confirm_received)
    {
        if (bsp_tick_get_ms() >= g_s_confirm_deadline_ms)
        {
            return false; /* таймаут */
        }
        bsp_usb_cdc_poll();
        cli_process();
    }

    return g_s_confirm_value;
}
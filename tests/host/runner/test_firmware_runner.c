/**
 * @file  test_firmware_runner.c
 * @brief Host unit-тесты state machine test_runner.c.
 *
 * Категория B (fff): мокируются protocol_send_*, bsp_tick_get_ms,
 * bsp_usb_cdc_poll, cli_process.
 *
 * UNIT_TEST seam: реестр тест-модулей предоставляется этим файлом
 * через g_unit_test_registry / g_unit_test_registry_size.
 * set_registry() меняет состав реестра между тестами.
 *
 * Паттерн захвата test_result: protocol_send_test_result получает указатель
 * на стековую переменную execute_test(). Копируем через custom_fake.
 */

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS;

/* ── Типы (нужны для сигнатур фейков) ─────────────────────────────────── */

#include "protocol.h"
#include "test_module.h"
/* ── Фейки зависимостей test_runner.c ─────────────────────────────────── */

FAKE_VALUE_FUNC(uint32_t, bsp_tick_get_ms);
FAKE_VOID_FUNC(bsp_delay, uint32_t);
FAKE_VOID_FUNC(bsp_usb_cdc_poll);
FAKE_VOID_FUNC(cli_process);

FAKE_VOID_FUNC(protocol_send_test_begin, const test_module_t *);
FAKE_VOID_FUNC(protocol_send_test_result, const test_module_t *, const test_result_t *);
FAKE_VOID_FUNC(protocol_send_summary, uint8_t, uint8_t, uint8_t, bool);
FAKE_VOID_FUNC(protocol_send_confirm_request, const confirm_params_t *);
FAKE_VOID_FUNC(protocol_send_error, const char *);
FAKE_VOID_FUNC(protocol_send_pong);
FAKE_VOID_FUNC(protocol_send_session_start);

/* ── Модуль под тестом ─────────────────────────────────────────────────── */

#include "test_runner.h"

/* ── Инъекция реестра ──────────────────────────────────────────────────── */

#define UNIT_TEST_REGISTRY_MAX 8U

const test_module_t *g_unit_test_registry[UNIT_TEST_REGISTRY_MAX];
size_t g_unit_test_registry_size = 0U;

static void set_registry(const test_module_t **pp_mods, size_t count)
{
    for (size_t i = 0U; i < count && i < UNIT_TEST_REGISTRY_MAX; i++)
    {
        g_unit_test_registry[i] = pp_mods[i];
    }
    g_unit_test_registry_size = count;
}

/* ── Тест-модули ───────────────────────────────────────────────────────── */

static test_result_t run_pass(void)
{
    test_result_t r = { .status = TEST_STATUS_PASS, .duration_ms = 0U };
    r.detail[0]     = '\0';
    return r;
}

static test_result_t run_fail(void)
{
    test_result_t r = { .status = TEST_STATUS_FAIL, .duration_ms = 0U };
    (void) snprintf(r.detail, TEST_DETAIL_SIZE, "%s", "simulated fail");
    return r;
}

static const test_module_t K_MOD_PASS = {
    .id                 = "t_pass",
    .name               = "Pass Test",
    .critical           = false,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = NULL,
    .run                = run_pass,
    .deinit             = NULL,
};

static const test_module_t K_MOD_CRIT_FAIL = {
    .id                 = "t_fail",
    .name               = "Critical Fail Test",
    .critical           = true,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = NULL,
    .run                = run_fail,
    .deinit             = NULL,
};

static const test_module_t K_MOD_PRECONFIRM = {
    .id                 = "t_pre",
    .name               = "Pre-confirm Test",
    .critical           = false,
    .requires_hil       = false,
    .pre_confirm_prompt = "Insert the device",
    .init               = NULL,
    .run                = run_pass,
    .deinit             = NULL,
};

/* ── Захват test_result (dangling pointer fix) ─────────────────────────── */

static test_result_t s_last_result;
static bool s_result_captured;

static void capture_test_result(const test_module_t *p_mod, const test_result_t *p_result)
{
    s_last_result     = *p_result; /* копия пока стек жив */
    s_result_captured = true;
    (void) p_mod;
}

/* ── setUp / tearDown ──────────────────────────────────────────────────── */

void setUp(void)
{
    RESET_FAKE(bsp_tick_get_ms);
    RESET_FAKE(bsp_delay);
    RESET_FAKE(bsp_usb_cdc_poll);
    RESET_FAKE(cli_process);
    RESET_FAKE(protocol_send_test_begin);
    RESET_FAKE(protocol_send_test_result);
    RESET_FAKE(protocol_send_summary);
    RESET_FAKE(protocol_send_confirm_request);
    RESET_FAKE(protocol_send_error);
    FFF_RESET_HISTORY();

    protocol_send_test_result_fake.custom_fake = capture_test_result;
    bsp_tick_get_ms_fake.return_val            = 0U;
    s_result_captured                          = false;

    g_unit_test_registry_size = 0U;
    test_runner_init();
}

void tearDown(void)
{
}

/* ── Тесты: базовые (пустой реестр) ───────────────────────────────────── */

void test_not_busy_initially(void)
{
    TEST_ASSERT_FALSE(test_runner_is_busy());
}

void test_run_all_empty_registry_sends_summary(void)
{
    test_runner_run_all();

    TEST_ASSERT_EQUAL_INT(1, protocol_send_summary_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, protocol_send_summary_fake.arg0_val); /* passed  */
    TEST_ASSERT_EQUAL_INT(0, protocol_send_summary_fake.arg1_val); /* failed  */
    TEST_ASSERT_EQUAL_INT(0, protocol_send_summary_fake.arg2_val); /* skipped */
    TEST_ASSERT_TRUE(protocol_send_summary_fake.arg3_val);         /* overall */
}

void test_run_single_unknown_id_sends_error(void)
{
    test_runner_run_single("nonexistent");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN_TEST", protocol_send_error_fake.arg0_val);
}

/* ── Тесты: одиночный запуск ───────────────────────────────────────────── */

void test_run_single_pass_sends_begin_and_result(void)
{
    const test_module_t *mods[] = { &K_MOD_PASS };
    set_registry(mods, 1U);

    test_runner_run_single("t_pass");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_test_begin_fake.call_count);
    TEST_ASSERT_TRUE(s_result_captured);
    TEST_ASSERT_EQUAL_INT(TEST_STATUS_PASS, s_last_result.status);
    TEST_ASSERT_FALSE(test_runner_is_busy());
}

void test_run_single_fail_sends_fail_result(void)
{
    const test_module_t *mods[] = { &K_MOD_CRIT_FAIL };
    set_registry(mods, 1U);

    test_runner_run_single("t_fail");

    TEST_ASSERT_TRUE(s_result_captured);
    TEST_ASSERT_EQUAL_INT(TEST_STATUS_FAIL, s_last_result.status);
}

/* ── Тесты: run_all ────────────────────────────────────────────────────── */

void test_run_all_two_pass_sends_correct_summary(void)
{
    const test_module_t *mods[] = { &K_MOD_PASS, &K_MOD_PASS };
    set_registry(mods, 2U);

    test_runner_run_all();

    TEST_ASSERT_EQUAL_INT(2, protocol_send_test_begin_fake.call_count);
    TEST_ASSERT_EQUAL_INT(2, protocol_send_test_result_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, protocol_send_summary_fake.call_count);
    TEST_ASSERT_EQUAL_INT(2, protocol_send_summary_fake.arg0_val); /* passed  */
    TEST_ASSERT_EQUAL_INT(0, protocol_send_summary_fake.arg1_val); /* failed  */
    TEST_ASSERT_TRUE(protocol_send_summary_fake.arg3_val);         /* overall */
}

void test_run_all_critical_fail_skips_remaining(void)
{
    /* [pass, critical_fail, pass] — третий должен получить SKIP */
    const test_module_t *mods[] = { &K_MOD_PASS, &K_MOD_CRIT_FAIL, &K_MOD_PASS };
    set_registry(mods, 3U);

    test_runner_run_all();

    /* Все три теста получили test_begin + test_result */
    TEST_ASSERT_EQUAL_INT(3, protocol_send_test_begin_fake.call_count);
    TEST_ASSERT_EQUAL_INT(3, protocol_send_test_result_fake.call_count);

    /* Summary: 1 pass, 1 fail, 1 skip, overall fail */
    TEST_ASSERT_EQUAL_INT(1, protocol_send_summary_fake.arg0_val);
    TEST_ASSERT_EQUAL_INT(1, protocol_send_summary_fake.arg1_val);
    TEST_ASSERT_EQUAL_INT(1, protocol_send_summary_fake.arg2_val);
    TEST_ASSERT_FALSE(protocol_send_summary_fake.arg3_val);
}

/* ── Тесты: pre_confirm state machine ─────────────────────────────────── */

void test_pre_confirm_module_enters_busy_state(void)
{
    const test_module_t *mods[] = { &K_MOD_PRECONFIRM };
    set_registry(mods, 1U);

    test_runner_run_single("t_pre");

    /* confirm_request отправлен, runner в PRE_CONFIRM */
    TEST_ASSERT_EQUAL_INT(1, protocol_send_confirm_request_fake.call_count);
    TEST_ASSERT_TRUE(test_runner_is_busy());
    /* Тест ещё не выполнен */
    TEST_ASSERT_EQUAL_INT(0, protocol_send_test_begin_fake.call_count);
}

void test_pre_confirm_busy_rejects_new_run(void)
{
    const test_module_t *mods[] = { &K_MOD_PRECONFIRM };
    set_registry(mods, 1U);

    test_runner_run_single("t_pre"); /* входим в PRE_CONFIRM */
    test_runner_run_single("t_pre"); /* должен получить BUSY */

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("BUSY", protocol_send_error_fake.arg0_val);
}

void test_pre_confirm_confirmed_true_executes_test(void)
{
    const test_module_t *mods[] = { &K_MOD_PRECONFIRM };
    set_registry(mods, 1U);

    test_runner_run_single("t_pre");
    test_runner_on_confirm("t_pre", true);
    test_runner_process(); /* confirm принят → тест выполняется */

    TEST_ASSERT_EQUAL_INT(1, protocol_send_test_begin_fake.call_count);
    TEST_ASSERT_TRUE(s_result_captured);
    TEST_ASSERT_EQUAL_INT(TEST_STATUS_PASS, s_last_result.status);
    TEST_ASSERT_FALSE(test_runner_is_busy());
}

void test_pre_confirm_confirmed_false_skips_test(void)
{
    const test_module_t *mods[] = { &K_MOD_PRECONFIRM };
    set_registry(mods, 1U);

    test_runner_run_single("t_pre");
    test_runner_on_confirm("t_pre", false);
    test_runner_process();

    TEST_ASSERT_TRUE(s_result_captured);
    TEST_ASSERT_EQUAL_INT(TEST_STATUS_SKIP, s_last_result.status);
    TEST_ASSERT_FALSE(test_runner_is_busy());
}

void test_pre_confirm_timeout_skips_test(void)
{
    const test_module_t *mods[] = { &K_MOD_PRECONFIRM };
    set_registry(mods, 1U);

    bsp_tick_get_ms_fake.return_val = 0U;
    test_runner_run_single("t_pre");

    /* Симулируем истечение таймаута */
    bsp_tick_get_ms_fake.return_val = PROTOCOL_CONFIRM_TIMEOUT_MS;
    test_runner_process();

    TEST_ASSERT_TRUE(s_result_captured);
    TEST_ASSERT_EQUAL_INT(TEST_STATUS_SKIP, s_last_result.status);
}

void test_stale_confirm_wrong_id_ignored(void)
{
    const test_module_t *mods[] = { &K_MOD_PRECONFIRM };
    set_registry(mods, 1U);

    test_runner_run_single("t_pre");
    test_runner_on_confirm("wrong_id", true); /* stale — не должен совпасть */
    test_runner_process();

    /* Тест не выполнен — confirm проигнорирован, всё ещё ждём */
    TEST_ASSERT_EQUAL_INT(0, protocol_send_test_begin_fake.call_count);
    TEST_ASSERT_TRUE(test_runner_is_busy());
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_not_busy_initially);
    RUN_TEST(test_run_all_empty_registry_sends_summary);
    RUN_TEST(test_run_single_unknown_id_sends_error);

    RUN_TEST(test_run_single_pass_sends_begin_and_result);
    RUN_TEST(test_run_single_fail_sends_fail_result);

    RUN_TEST(test_run_all_two_pass_sends_correct_summary);
    RUN_TEST(test_run_all_critical_fail_skips_remaining);

    RUN_TEST(test_pre_confirm_module_enters_busy_state);
    RUN_TEST(test_pre_confirm_busy_rejects_new_run);
    RUN_TEST(test_pre_confirm_confirmed_true_executes_test);
    RUN_TEST(test_pre_confirm_confirmed_false_skips_test);
    RUN_TEST(test_pre_confirm_timeout_skips_test);
    RUN_TEST(test_stale_confirm_wrong_id_ignored);

    return UNITY_END();
}
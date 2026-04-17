/**
 * @file  test_protocol.c
 * @brief Host unit-тесты для protocol.c — сериализация JSON-событий.
 *
 * Категория B (fff): две зависимости мокируются через fff.
 *   cli_send()          — захватываем строку через custom_fake.
 *   bsp_tick_get_ms()   — контролируем uptime в session_start.
 *
 * Паттерн захвата строки: custom_fake копирует аргумент в s_captured
 * до возврата protocol_send_*(), пока стековый буфер ещё жив.
 */

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS;

/* ── Фейки зависимостей protocol.c ────────────────────────────────────── */

FAKE_VOID_FUNC(cli_send, const char *);
FAKE_VOID_FUNC(bsp_delay, uint32_t);
FAKE_VALUE_FUNC(uint32_t, bsp_tick_get_ms);

/* ── Модуль под тестом ─────────────────────────────────────────────────── */

#include "protocol.h"

/* ── Захват вывода cli_send ────────────────────────────────────────────── */

#define CAPTURE_BUF_SIZE 256U

static char s_captured[CAPTURE_BUF_SIZE];

static void capture_cli_send(const char *p_resp)
{
    (void) snprintf(s_captured, sizeof(s_captured), "%s", p_resp);
}

/* ── Вспомогательные данные ────────────────────────────────────────────── */

/* Минимальный дескриптор — protocol_send_* использует только нужные поля */
static const test_module_t K_MOD_SDRAM = {
    .id                 = "sdram",
    .name               = "SDRAM 32 MB",
    .critical           = true,
    .requires_hil       = false,
    .pre_confirm_prompt = NULL,
    .init               = NULL,
    .run                = NULL,
    .deinit             = NULL,
};

static const test_module_t K_MOD_OPTO = {
    .id                 = "opto",
    .name               = "Opto-in EXT",
    .critical           = false,
    .requires_hil       = true,
    .pre_confirm_prompt = NULL,
    .init               = NULL,
    .run                = NULL,
    .deinit             = NULL,
};

/* ── setUp / tearDown ──────────────────────────────────────────────────── */

void setUp(void)
{
    RESET_FAKE(cli_send);
    RESET_FAKE(bsp_tick_get_ms);
    RESET_FAKE(bsp_delay);
    FFF_RESET_HISTORY();
    cli_send_fake.custom_fake       = capture_cli_send;
    bsp_tick_get_ms_fake.return_val = 0U;
    s_captured[0]                   = '\0';
}

void tearDown(void)
{
}

/* ── Тесты: session_start ──────────────────────────────────────────────── */

void test_session_start_format_zero_uptime(void)
{
    bsp_tick_get_ms_fake.return_val = 0U;

    protocol_send_session_start();

    TEST_ASSERT_EQUAL_INT(1, cli_send_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"session_start\",\"fw\":\"" FIRMWARE_TEST_VERSION "\","
                             "\"target\":\"IMXRT1052\",\"uptime_ms\":0}\n",
                             s_captured);
}

void test_session_start_uptime_nonzero(void)
{
    bsp_tick_get_ms_fake.return_val = 1234U;

    protocol_send_session_start();

    TEST_ASSERT_NOT_NULL(strstr(s_captured, "\"uptime_ms\":1234"));
}

/* ── Тесты: test_begin ─────────────────────────────────────────────────── */

void test_test_begin_critical_true(void)
{
    protocol_send_test_begin(&K_MOD_SDRAM);

    TEST_ASSERT_EQUAL_INT(1, cli_send_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"test_begin\",\"id\":\"sdram\","
                             "\"name\":\"SDRAM 32 MB\",\"critical\":true}\n",
                             s_captured);
}

void test_test_begin_critical_false(void)
{
    protocol_send_test_begin(&K_MOD_OPTO);

    TEST_ASSERT_EQUAL_STRING("{\"type\":\"test_begin\",\"id\":\"opto\","
                             "\"name\":\"Opto-in EXT\",\"critical\":false}\n",
                             s_captured);
}

/* ── Тесты: test_result ────────────────────────────────────────────────── */

void test_test_result_pass(void)
{
    test_result_t result = {
        .status      = TEST_STATUS_PASS,
        .duration_ms = 312U,
        .detail      = "",
    };

    protocol_send_test_result(&K_MOD_SDRAM, &result);

    TEST_ASSERT_EQUAL_INT(1, cli_send_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"test_result\",\"id\":\"sdram\","
                             "\"status\":\"pass\",\"ms\":312,\"detail\":\"\"}\n",
                             s_captured);
}

void test_test_result_fail_with_detail(void)
{
    test_result_t result = {
        .status      = TEST_STATUS_FAIL,
        .duration_ms = 88U,
        .detail      = "addr=0x80001000 expected=0xA5 got=0x00",
    };

    protocol_send_test_result(&K_MOD_SDRAM, &result);

    TEST_ASSERT_EQUAL_STRING("{\"type\":\"test_result\",\"id\":\"sdram\","
                             "\"status\":\"fail\",\"ms\":88,"
                             "\"detail\":\"addr=0x80001000 expected=0xA5 got=0x00\"}\n",
                             s_captured);
}

void test_test_result_skip(void)
{
    test_result_t result = {
        .status      = TEST_STATUS_SKIP,
        .duration_ms = 0U,
        .detail      = "confirm timeout",
    };

    protocol_send_test_result(&K_MOD_OPTO, &result);

    TEST_ASSERT_NOT_NULL(strstr(s_captured, "\"status\":\"skip\""));
    TEST_ASSERT_NOT_NULL(strstr(s_captured, "\"detail\":\"confirm timeout\""));
}

/* ── Тесты: summary ────────────────────────────────────────────────────── */

void test_summary_overall_pass(void)
{
    protocol_send_summary(6U, 0U, 1U, true);

    TEST_ASSERT_EQUAL_INT(1, cli_send_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"summary\",\"passed\":6,\"failed\":0,"
                             "\"skipped\":1,\"overall\":\"pass\"}\n",
                             s_captured);
}

void test_summary_overall_fail(void)
{
    protocol_send_summary(5U, 1U, 0U, false);

    TEST_ASSERT_EQUAL_STRING("{\"type\":\"summary\",\"passed\":5,\"failed\":1,"
                             "\"skipped\":0,\"overall\":\"fail\"}\n",
                             s_captured);
}

/* ── Тесты: confirm_request ────────────────────────────────────────────── */

void test_confirm_request_custom_timeout(void)
{
    confirm_params_t params = {
        .id         = "usd_insert",
        .prompt     = "Insert microSD card",
        .timeout_ms = 30000U,
    };

    protocol_send_confirm_request(&params);

    TEST_ASSERT_EQUAL_INT(1, cli_send_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"confirm_request\",\"id\":\"usd_insert\","
                             "\"prompt\":\"Insert microSD card\",\"timeout_ms\":30000}\n",
                             s_captured);
}

void test_confirm_request_default_timeout_on_zero(void)
{
    confirm_params_t params = {
        .id         = "display_red",
        .prompt     = "Red?",
        .timeout_ms = 0U,
    };

    protocol_send_confirm_request(&params);

    /* timeout_ms == 0 → подставляется PROTOCOL_CONFIRM_TIMEOUT_MS */
    TEST_ASSERT_NOT_NULL(strstr(s_captured, "\"timeout_ms\":15000"));
}

/* ── Тесты: pong ───────────────────────────────────────────────────────── */

void test_pong_exact_string(void)
{
    protocol_send_pong();

    TEST_ASSERT_EQUAL_INT(1, cli_send_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"pong\"}\n", s_captured);
}

/* ── Тесты: error ──────────────────────────────────────────────────────── */

void test_error_parse_err(void)
{
    protocol_send_error("PARSE_ERR");

    TEST_ASSERT_EQUAL_INT(1, cli_send_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"PARSE_ERR\"}\n", s_captured);
}

void test_error_unknown_test(void)
{
    protocol_send_error("UNKNOWN_TEST");

    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"UNKNOWN_TEST\"}\n", s_captured);
}

/* ── Точка входа ───────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_session_start_format_zero_uptime);
    RUN_TEST(test_session_start_uptime_nonzero);

    RUN_TEST(test_test_begin_critical_true);
    RUN_TEST(test_test_begin_critical_false);

    RUN_TEST(test_test_result_pass);
    RUN_TEST(test_test_result_fail_with_detail);
    RUN_TEST(test_test_result_skip);

    RUN_TEST(test_summary_overall_pass);
    RUN_TEST(test_summary_overall_fail);

    RUN_TEST(test_confirm_request_custom_timeout);
    RUN_TEST(test_confirm_request_default_timeout_on_zero);

    RUN_TEST(test_pong_exact_string);

    RUN_TEST(test_error_parse_err);
    RUN_TEST(test_error_unknown_test);

    return UNITY_END();
}
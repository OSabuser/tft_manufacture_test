/**
 * @file  test_cli.c
 * @brief Host unit-тесты IO-слоя cli.c.
 *
 * Категория B (fff): мокируются bsp_usb_cdc, protocol_send_*, test_runner_*.
 *
 * Паттерн ввода: inject() наполняет внутренний буфер «как USB», затем
 * вызывает cli_process() — полностью изолируем I/O.
 *
 * Паттерн захвата строковых аргументов: custom_fake копирует id/cmd
 * пока стек ещё жив (аргументы — указатели на локальные буферы cli.c).
 */

#include "bsp/provisioning.h"
#include "bsp/status.h"
#include "fff.h"
#include "test_module.h"
#include "unity.h"

#include <stdbool.h>
DEFINE_FFF_GLOBALS;

/* ── Фейки зависимостей cli.c ──────────────────────────────────────────── */

FAKE_VOID_FUNC(bsp_usb_cdc_write, const uint8_t *, size_t);
FAKE_VALUE_FUNC(size_t, bsp_usb_cdc_read, uint8_t *, size_t);

FAKE_VOID_FUNC(protocol_send_pong);
FAKE_VOID_FUNC(protocol_send_error, const char *);

FAKE_VOID_FUNC(test_runner_run_all);
FAKE_VOID_FUNC(test_runner_run_single, const char *);
FAKE_VOID_FUNC(test_runner_on_confirm, const char *, bool);
FAKE_VOID_FUNC(protocol_send_uid_response, const uint8_t *);
FAKE_VOID_FUNC(protocol_send_version_response);
FAKE_VOID_FUNC(test_runner_run_selected, const char *const *, size_t);
FAKE_VOID_FUNC(test_runner_send_list);
FAKE_VALUE_FUNC(bsp_status_t, bsp_prov_read_uid, uint8_t *, size_t);
/* ── Модуль под тестом ─────────────────────────────────────────────────── */

#include "cli.h"

/* ── Инъекция ввода ────────────────────────────────────────────────────── */

#define INJECT_BUF_SIZE (CLI_LINE_BUF_SIZE * 2U)

static uint8_t s_inject_buf[INJECT_BUF_SIZE];
static size_t s_inject_len = 0U;

static size_t fake_usb_read(uint8_t *p_buf, size_t size)
{
    if (s_inject_len == 0U)
    {
        return 0U;
    }
    size_t to_copy = (s_inject_len < size) ? s_inject_len : size;
    memcpy(p_buf, s_inject_buf, to_copy);
    s_inject_len = 0U;
    return to_copy;
}

static void inject(const char *p_line)
{
    size_t len   = strlen(p_line);
    size_t limit = (len < INJECT_BUF_SIZE) ? len : INJECT_BUF_SIZE - 1U;
    memcpy(s_inject_buf, p_line, limit);
    s_inject_len = limit;
    cli_process();
}

/* ── Захват аргументов-строк ───────────────────────────────────────────── */
/*
 * test_runner_run_single и test_runner_on_confirm получают указатели
 * на локальные буферы cli.c. После возврата cli_process() стек мёртв —
 * нельзя читать fake.arg0_val. Копируем через custom_fake.
 */

static char s_run_single_id[32U];
static char s_confirm_id[32U];
static bool s_confirm_value;

/* get_uid: cli.c передаёт указатель на СВОЙ стековый буфер — копируем
 * по значению внутри custom_fake, иначе после возврата cli_process()
 * указатель уже dangling (см. "Ловушка" в HOST_CREATE_TEST.md). */
static uint8_t s_captured_uid[BSP_PROV_UID_LEN];

/* run_selected: id_ptrs[] в cli.c указывает на локальный id_bufs[][] —
 * та же ловушка, копируем строки, а не указатели. */
static size_t s_run_selected_count;
static char s_run_selected_ids[TEST_REGISTRY_MAX_SIZE][TEST_ID_MAX_SIZE];

static void capture_run_single(const char *p_id)
{
    (void) snprintf(s_run_single_id, sizeof(s_run_single_id), "%s", p_id);
}

static void capture_on_confirm(const char *p_id, bool confirmed)
{
    (void) snprintf(s_confirm_id, sizeof(s_confirm_id), "%s", p_id);
    s_confirm_value = confirmed;
}

static void capture_uid_response(const uint8_t *p_uid)
{
    memcpy(s_captured_uid, p_uid, BSP_PROV_UID_LEN);
}

static void capture_run_selected(const char *const *pp_ids, size_t count)
{
    s_run_selected_count = count;
    for (size_t i = 0U; i < count && i < TEST_REGISTRY_MAX_SIZE; i++)
    {
        (void) snprintf(s_run_selected_ids[i], sizeof(s_run_selected_ids[i]), "%s", pp_ids[i]);
    }
}

/* Фиксированный UID, который "читает" bsp_prov_read_uid в тестах успеха. */
static const uint8_t K_TEST_UID[BSP_PROV_UID_LEN] = {
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22
};

static bsp_status_t prov_read_uid_success(uint8_t *p_uid, size_t len)
{
    (void) len;
    memcpy(p_uid, K_TEST_UID, BSP_PROV_UID_LEN);
    return BSP_OK;
}
/* ── setUp / tearDown ──────────────────────────────────────────────────── */

void setUp(void)
{
    RESET_FAKE(bsp_usb_cdc_read);
    RESET_FAKE(bsp_usb_cdc_write);
    RESET_FAKE(protocol_send_pong);
    RESET_FAKE(protocol_send_error);
    RESET_FAKE(test_runner_run_all);
    RESET_FAKE(test_runner_run_single);
    RESET_FAKE(test_runner_on_confirm);
    RESET_FAKE(protocol_send_uid_response);
    RESET_FAKE(protocol_send_version_response);
    RESET_FAKE(bsp_prov_read_uid);
    RESET_FAKE(protocol_send_version_response);
    RESET_FAKE(bsp_prov_read_uid);
    RESET_FAKE(test_runner_run_selected);
    RESET_FAKE(test_runner_send_list);
    FFF_RESET_HISTORY();

    bsp_usb_cdc_read_fake.custom_fake           = fake_usb_read;
    test_runner_run_single_fake.custom_fake     = capture_run_single;
    test_runner_on_confirm_fake.custom_fake     = capture_on_confirm;
    protocol_send_uid_response_fake.custom_fake = capture_uid_response;
    test_runner_run_selected_fake.custom_fake   = capture_run_selected;

    s_inject_len         = 0U;
    s_run_single_id[0]   = '\0';
    s_confirm_id[0]      = '\0';
    s_confirm_value      = false;
    s_run_selected_count = 0U;
    (void) memset(s_captured_uid, 0, sizeof(s_captured_uid));

    cli_init();
}

void tearDown(void)
{
}

/* ── Тесты: type = cmd ─────────────────────────────────────────────────── */

void test_ping_dispatches_to_pong(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"ping\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_pong_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, protocol_send_error_fake.call_count);
}

void test_run_all_dispatches_to_runner(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"run_all\"}\n");

    TEST_ASSERT_EQUAL_INT(1, test_runner_run_all_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, protocol_send_error_fake.call_count);
}

void test_run_single_dispatches_with_id(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"run\",\"id\":\"sdram\"}\n");

    TEST_ASSERT_EQUAL_INT(1, test_runner_run_single_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("sdram", s_run_single_id);
}

void test_get_uid_success(void)
{
    bsp_prov_read_uid_fake.custom_fake = prov_read_uid_success;

    inject("{\"type\":\"cmd\",\"cmd\":\"get_uid\"}\n");

    TEST_ASSERT_EQUAL_INT(1, bsp_prov_read_uid_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, protocol_send_uid_response_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(K_TEST_UID, s_captured_uid, BSP_PROV_UID_LEN);
}

void test_get_uid_read_failure_sends_error(void)
{
    bsp_prov_read_uid_fake.return_val = BSP_ERR_PARAM;

    inject("{\"type\":\"cmd\",\"cmd\":\"get_uid\"}\n");

    TEST_ASSERT_EQUAL_INT(0, protocol_send_uid_response_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("UID_READ_ERR", protocol_send_error_fake.arg0_val);
}

void test_get_version_dispatches(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"get_version\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_version_response_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, protocol_send_error_fake.call_count);
}

void test_list_tests_dispatches(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"list_tests\"}\n");

    TEST_ASSERT_EQUAL_INT(1, test_runner_send_list_fake.call_count);
}

void test_run_selected_dispatches_with_ids(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"run_selected\",\"tests\":[\"sdram\",\"opto\"]}\n");

    TEST_ASSERT_EQUAL_INT(1, test_runner_run_selected_fake.call_count);
    TEST_ASSERT_EQUAL(2, s_run_selected_count);
    TEST_ASSERT_EQUAL_STRING("sdram", s_run_selected_ids[0]);
    TEST_ASSERT_EQUAL_STRING("opto", s_run_selected_ids[1]);
}

void test_run_selected_missing_tests_field_sends_parse_err(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"run_selected\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("PARSE_ERR", protocol_send_error_fake.arg0_val);
}

void test_unknown_cmd_sends_unknown_cmd_error(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"reboot\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN_CMD", protocol_send_error_fake.arg0_val);
}

void test_run_missing_id_sends_parse_err(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"run\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("PARSE_ERR", protocol_send_error_fake.arg0_val);
}

/* ── Тесты: type = confirm ─────────────────────────────────────────────── */

void test_confirm_true_dispatches(void)
{
    inject("{\"type\":\"confirm\",\"id\":\"display_red\",\"confirmed\":true}\n");

    TEST_ASSERT_EQUAL_INT(1, test_runner_on_confirm_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("display_red", s_confirm_id);
    TEST_ASSERT_TRUE(s_confirm_value);
}

void test_confirm_false_dispatches(void)
{
    inject("{\"type\":\"confirm\",\"id\":\"usd_insert\",\"confirmed\":false}\n");

    TEST_ASSERT_EQUAL_INT(1, test_runner_on_confirm_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("usd_insert", s_confirm_id);
    TEST_ASSERT_FALSE(s_confirm_value);
}

void test_confirm_missing_confirmed_field_sends_parse_err(void)
{
    inject("{\"type\":\"confirm\",\"id\":\"display_red\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("PARSE_ERR", protocol_send_error_fake.arg0_val);
}

/* ── Тесты: ошибки протокола ───────────────────────────────────────────── */

void test_missing_type_field_sends_parse_err(void)
{
    inject("{\"cmd\":\"ping\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("PARSE_ERR", protocol_send_error_fake.arg0_val);
}

void test_unknown_type_sends_unknown_cmd(void)
{
    inject("{\"type\":\"status\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("UNKNOWN_CMD", protocol_send_error_fake.arg0_val);
}

void test_line_too_long_sends_error(void)
{
    /* CLI_LINE_BUF_SIZE - 1 символов без '\n' = переполнение буфера */
    char long_line[CLI_LINE_BUF_SIZE + 2U];
    memset(long_line, 'x', CLI_LINE_BUF_SIZE);
    long_line[CLI_LINE_BUF_SIZE]      = '\n';
    long_line[CLI_LINE_BUF_SIZE + 1U] = '\0';

    inject(long_line);

    TEST_ASSERT_EQUAL_INT(1, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_STRING("LINE_TOO_LONG", protocol_send_error_fake.arg0_val);
}

void test_empty_line_ignored(void)
{
    inject("\n");

    TEST_ASSERT_EQUAL_INT(0, protocol_send_pong_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, protocol_send_error_fake.call_count);
    TEST_ASSERT_EQUAL_INT(0, test_runner_run_all_fake.call_count);
}

void test_crlf_handled_same_as_lf(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"ping\"}\r\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_pong_fake.call_count);
}

void test_two_lines_in_one_chunk_both_dispatched(void)
{
    inject("{\"type\":\"cmd\",\"cmd\":\"ping\"}\n"
           "{\"type\":\"cmd\",\"cmd\":\"run_all\"}\n");

    TEST_ASSERT_EQUAL_INT(1, protocol_send_pong_fake.call_count);
    TEST_ASSERT_EQUAL_INT(1, test_runner_run_all_fake.call_count);
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_ping_dispatches_to_pong);
    RUN_TEST(test_run_all_dispatches_to_runner);
    RUN_TEST(test_run_single_dispatches_with_id);
    RUN_TEST(test_unknown_cmd_sends_unknown_cmd_error);
    RUN_TEST(test_run_missing_id_sends_parse_err);

    RUN_TEST(test_confirm_true_dispatches);
    RUN_TEST(test_confirm_false_dispatches);
    RUN_TEST(test_confirm_missing_confirmed_field_sends_parse_err);

    RUN_TEST(test_missing_type_field_sends_parse_err);
    RUN_TEST(test_unknown_type_sends_unknown_cmd);
    RUN_TEST(test_line_too_long_sends_error);
    RUN_TEST(test_empty_line_ignored);
    RUN_TEST(test_crlf_handled_same_as_lf);
    RUN_TEST(test_two_lines_in_one_chunk_both_dispatched);

    RUN_TEST(test_get_uid_success);
    RUN_TEST(test_get_uid_read_failure_sends_error);
    RUN_TEST(test_get_version_dispatches);
    RUN_TEST(test_list_tests_dispatches);
    RUN_TEST(test_run_selected_dispatches_with_ids);
    RUN_TEST(test_run_selected_missing_tests_field_sends_parse_err);

    return UNITY_END();
}
/**
 * @file  test_cli.c
 * @brief Host unit-тест для firmware_test/src/cli.c
 *
 * Тестирует: parse_cmd_field (через cli_process), dispatch, обработку
 * граничных случаев буфера. Без железа, без fff — stub-ы для
 * bsp_usb_cdc_read/write определены прямо здесь.
 *
 * Добавление теста для новой команды:
 *   1. stub_rx_feed() — подать строку в RX буфер.
 *   2. cli_process()  — запустить.
 *   3. TEST_ASSERT_EQUAL_STRING() — проверить TX буфер.
 */

#include "bsp/status.h"
#include "bsp/usb_cdc.h"
#include "unity.h"

#include <stdint.h>
#include <string.h>

/* ── Stub: bsp_usb_cdc ─────────────────────────────────────────────────
 *
 * RX: тест кладёт байты через stub_rx_feed().
 *     cli_process() читает их по одному через bsp_usb_cdc_read().
 *
 * TX: cli_send() пишет через bsp_usb_cdc_write() в s_tx_buf.
 *     тест проверяет s_tx_buf через stub_tx_get().
 *
 * ────────────────────────────────────────────────────────────────────── */

static uint8_t s_rx_buf[256];
static size_t s_rx_len = 0U;
static size_t s_rx_pos = 0U;

static char s_tx_buf[512];
static size_t s_tx_len = 0U;

/** @brief Заполнить RX буфер данными для теста. */
static void stub_rx_feed(const char *data)
{
    size_t len = strlen(data);
    memcpy(s_rx_buf, data, len);
    s_rx_len = len;
    s_rx_pos = 0U;
}

/** @brief Получить TX буфер как C-строку. */
static const char *stub_tx_get(void)
{
    s_tx_buf[s_tx_len] = '\0';
    return s_tx_buf;
}

/** @brief Сбросить оба буфера. */
static void stub_reset(void)
{
    s_rx_len = 0U;
    s_rx_pos = 0U;
    s_tx_len = 0U;
    memset(s_tx_buf, 0, sizeof(s_tx_buf));
}

/* bsp_usb_cdc stubs */

size_t bsp_usb_cdc_read(uint8_t *buf, size_t max_len)
{
    size_t available = s_rx_len - s_rx_pos;
    size_t to_copy   = (available < max_len) ? available : max_len;
    memcpy(buf, s_rx_buf + s_rx_pos, to_copy);
    s_rx_pos += to_copy;
    return to_copy;
}

bsp_status_t bsp_usb_cdc_write(const uint8_t *data, size_t len)
{
    if ((s_tx_len + len) < sizeof(s_tx_buf))
    {
        memcpy(s_tx_buf + s_tx_len, data, len);
        s_tx_len += len;
    }
    return BSP_OK;
}

/* Остальные символы bsp_usb_cdc — заглушки, cli.c не вызывает их */
bsp_status_t bsp_usb_cdc_init(void)
{
    return BSP_OK;
}
bool bsp_usb_cdc_is_ready(void)
{
    return true;
}
bool bsp_usb_cdc_write_ready(void)
{
    return true;
}
void bsp_usb_cdc_poll(void)
{
}

/* ── Включаем тестируемый модуль ПОСЛЕ stub-ов ─────────────────────── */

#include "cli.c" /* NOLINT(bugprone-suspicious-include) */

/* ── Фикстуры ──────────────────────────────────────────────────────── */

void setUp(void)
{
    stub_reset();
    cli_init();
}

void tearDown(void)
{
}

/* ── Тесты ─────────────────────────────────────────────────────────── */

void test_ping_returns_pong(void)
{
    stub_rx_feed("{\"cmd\":\"PING\"}\n");
    cli_process();
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"result\":\"PONG\"}\n", stub_tx_get());
}

void test_unknown_command(void)
{
    stub_rx_feed("{\"cmd\":\"FOOBAR\"}\n");
    cli_process();
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"UNKNOWN_CMD\"}\n", stub_tx_get());
}

void test_missing_cmd_field(void)
{
    stub_rx_feed("{\"foo\":\"bar\"}\n");
    cli_process();
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"PARSE_ERR\"}\n", stub_tx_get());
}

void test_empty_line_no_response(void)
{
    /* Пустая строка '\n' — буфер пуст, dispatch не вызывается */
    stub_rx_feed("\n");
    cli_process();
    TEST_ASSERT_EQUAL_STRING("", stub_tx_get());
}

void test_line_too_long(void)
{
    /* 127 байт + '\n' = ровно граница CLI_LINE_BUF_SIZE */
    char long_line[CLI_LINE_BUF_SIZE + 2U];
    memset(long_line, 'A', CLI_LINE_BUF_SIZE);
    long_line[CLI_LINE_BUF_SIZE]      = '\n';
    long_line[CLI_LINE_BUF_SIZE + 1U] = '\0';

    stub_rx_feed(long_line);
    cli_process();
    TEST_ASSERT_EQUAL_STRING("{\"ok\":false,\"error\":\"LINE_TOO_LONG\"}\n", stub_tx_get());
}

void test_two_commands_in_sequence(void)
{
    /* Два PING подряд — оба должны дать PONG */
    stub_rx_feed("{\"cmd\":\"PING\"}\n{\"cmd\":\"PING\"}\n");
    cli_process();
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"result\":\"PONG\"}\n"
                             "{\"ok\":true,\"result\":\"PONG\"}\n",
                             /* Нет: первый вызов — первый PONG. Сбросим и проверим второй. */
                             stub_tx_get());
}

void test_cmd_field_with_spaces(void)
{
    /* Пробелы вокруг ':' — реальные клиенты могут так форматировать */
    stub_rx_feed("{\"cmd\" : \"PING\"}\n");
    cli_process();
    TEST_ASSERT_EQUAL_STRING("{\"ok\":true,\"result\":\"PONG\"}\n", stub_tx_get());
}

void test_partial_input_no_response_until_newline(void)
{
    /* Подать строку без '\n' — ответа быть не должно */
    stub_rx_feed("{\"cmd\":\"PING\"}");
    cli_process();
    TEST_ASSERT_EQUAL_STRING("", stub_tx_get());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_ping_returns_pong);
    RUN_TEST(test_unknown_command);
    RUN_TEST(test_missing_cmd_field);
    RUN_TEST(test_empty_line_no_response);
    RUN_TEST(test_line_too_long);
    RUN_TEST(test_two_commands_in_sequence);
    RUN_TEST(test_cmd_field_with_spaces);
    RUN_TEST(test_partial_input_no_response_until_newline);
    return UNITY_END();
}
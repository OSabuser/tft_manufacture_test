/**
 * @file  cli.c
 * @brief IO-слой и диспатчер сообщений v2 для firmware_test.
 *
 * Транспорт: USB CDC ACM через bsp_usb_cdc.
 *
 * Парсинг минималистичный: strstr по фиксированным полям.
 * cJSON не используется намеренно — схема входящих сообщений фиксирована.
 *
 * Входящие типы:
 *   "cmd"     → handle_cmd()     → test_runner или protocol_send_pong()
 *   "confirm" → handle_confirm() → test_runner_on_confirm()
 *
 * Добавление новой команды типа "cmd":
 *   1. Добавить ветку if (strcmp(cmd_name, "FOO") == 0) в handle_cmd().
 *   2. Вызвать нужный обработчик из test_runner.h или protocol.h.
 *
 * Добавление нового входящего типа:
 *   1. Добавить static void handle_<type>(const char *) ниже.
 *   2. Добавить ветку if (strcmp(msg_type, "<type>") == 0) в process_line().
 */

#include "cli.h"

#include "bsp/usb_cdc.h"
#include "protocol.h"
#include "test_runner.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ── Ключи полей JSON ──────────────────────────────────────────────────── */

static const char K_FIELD_TYPE[]      = "\"type\"";
static const char K_FIELD_CMD[]       = "\"cmd\"";
static const char K_FIELD_ID[]        = "\"id\"";
static const char K_FIELD_CONFIRMED[] = "\"confirmed\"";

/** @brief Буфер непрочитанного остатка chunk после вызова process_line(). */
static uint8_t g_s_chunk_buf[CLI_LINE_BUF_SIZE];
static size_t g_s_chunk_len = 0U;
static size_t g_s_chunk_pos = 0U;

/* ── RX line buffer ────────────────────────────────────────────────────── */

static uint8_t g_s_line_buf[CLI_LINE_BUF_SIZE];
static size_t g_s_line_len = 0U;

/* ── Парсинг полей ─────────────────────────────────────────────────────── */

/**
 * @brief Извлечь строковое значение в кавычках после двоеточия.
 *
 * @param[in]  p_after_key  Позиция сразу после ключа в строке JSON.
 * @param[out] p_out        Буфер для результата.
 * @param[in]  out_size     Размер p_out (включая место под '\0').
 * @return true если значение найдено и помещается в p_out.
 */
static bool extract_string_value(const char *p_after_key, char *p_out, size_t out_size)
{
    const char *colon = strchr(p_after_key, ':');
    if (colon == NULL)
    {
        return false;
    }

    const char *open_q = strchr(colon + 1U, '"');
    if (open_q == NULL)
    {
        return false;
    }
    open_q++;

    const char *close_q = strchr(open_q, '"');
    if (close_q == NULL)
    {
        return false;
    }

    size_t len = (size_t) (close_q - open_q);
    if (len == 0U || len >= out_size)
    {
        return false;
    }

    memcpy(p_out, open_q, len);
    p_out[len] = '\0';
    return true;
}

/**
 * @brief Извлечь значение поля "type".
 */
static bool parse_type_field(const char *p_line, char *p_out, size_t out_size)
{
    const char *key = strstr(p_line, K_FIELD_TYPE);
    if (key == NULL)
    {
        return false;
    }
    return extract_string_value(key + sizeof(K_FIELD_TYPE) - 1U, p_out, out_size);
}

/**
 * @brief Извлечь значение поля "cmd".
 */
static bool parse_cmd_field(const char *p_line, char *p_out, size_t out_size)
{
    const char *key = strstr(p_line, K_FIELD_CMD);
    if (key == NULL)
    {
        return false;
    }
    return extract_string_value(key + sizeof(K_FIELD_CMD) - 1U, p_out, out_size);
}

/**
 * @brief Извлечь значение поля "id".
 */
static bool parse_id_field(const char *p_line, char *p_out, size_t out_size)
{
    const char *key = strstr(p_line, K_FIELD_ID);
    if (key == NULL)
    {
        return false;
    }
    return extract_string_value(key + sizeof(K_FIELD_ID) - 1U, p_out, out_size);
}

/**
 * @brief Извлечь булево значение поля "confirmed".
 *
 * Ищет "confirmed":true или "confirmed":false без пробелов после двоеточия.
 *
 * @param[in]  p_line  NULL-terminated входная строка.
 * @param[out] p_out   Результат (true/false).
 * @return true если поле найдено и значение распознано.
 */
static bool parse_confirmed_field(const char *p_line, bool *p_out)
{
    const char *key = strstr(p_line, K_FIELD_CONFIRMED);
    if (key == NULL)
    {
        return false;
    }

    const char *colon = strchr(key + sizeof(K_FIELD_CONFIRMED) - 1U, ':');
    if (colon == NULL)
    {
        return false;
    }

    colon++;
    while (*colon == ' ')
    {
        colon++;
    }

    if (strncmp(colon, "true", sizeof("true") - 1U) == 0)
    {
        *p_out = true;
        return true;
    }

    if (strncmp(colon, "false", sizeof("false") - 1U) == 0)
    {
        *p_out = false;
        return true;
    }

    return false;
}

/* ── Обработчики входящих сообщений ────────────────────────────────────── */

/**
 * @brief Обработать команду "run": извлечь id и передать в test_runner.
 */
static void handle_cmd_run(const char *p_line)
{
    const uint8_t MAX_ID_LEN = 32U;
    char test_id[MAX_ID_LEN];

    if (!parse_id_field(p_line, test_id, sizeof(test_id)))
    {
        protocol_send_error("PARSE_ERR");
        return;
    }

    test_runner_run_single(test_id);
}

/**
 * @brief Обработать сообщение {"type":"cmd",...}.
 *
 * Команды: ping → pong, run_all → test_runner, run → test_runner.
 */
static void handle_cmd(const char *p_line)
{
    const uint8_t MAX_CMD_LEN = 32U;
    char cmd_name[MAX_CMD_LEN];

    if (!parse_cmd_field(p_line, cmd_name, sizeof(cmd_name)))
    {
        protocol_send_error("PARSE_ERR");
        return;
    }

    if (strcmp(cmd_name, "ping") == 0)
    {
        protocol_send_pong();
        return;
    }

    if (strcmp(cmd_name, "run_all") == 0)
    {
        test_runner_run_all();
        return;
    }

    if (strcmp(cmd_name, "run") == 0)
    {
        handle_cmd_run(p_line);
        return;
    }

    protocol_send_error("UNKNOWN_CMD");
}

/**
 * @brief Обработать сообщение {"type":"confirm",...}.
 *
 * Извлекает id и confirmed, передаёт в test_runner_on_confirm().
 */
static void handle_confirm(const char *p_line)
{
    const uint8_t MAX_ID_LEN = 32U;
    char id_buf[MAX_ID_LEN];
    bool confirmed = false;

    if (!parse_id_field(p_line, id_buf, sizeof(id_buf)))
    {
        protocol_send_error("PARSE_ERR");
        return;
    }

    if (!parse_confirmed_field(p_line, &confirmed))
    {
        protocol_send_error("PARSE_ERR");
        return;
    }

    test_runner_on_confirm(id_buf, confirmed);
}

/**
 * @brief Диспатчить накопленную строку по полю "type".
 */
static void process_line(const char *p_line)
{
    const uint8_t MAX_TYPE_LEN = 16U;
    char msg_type[MAX_TYPE_LEN];

    if (!parse_type_field(p_line, msg_type, sizeof(msg_type)))
    {
        protocol_send_error("PARSE_ERR");
        return;
    }

    if (strcmp(msg_type, "cmd") == 0)
    {
        handle_cmd(p_line);
        return;
    }

    if (strcmp(msg_type, "confirm") == 0)
    {
        handle_confirm(p_line);
        return;
    }

    protocol_send_error("UNKNOWN_CMD");
}

/* ── Public API ────────────────────────────────────────────────────────── */

void cli_init(void)
{
    g_s_line_len  = 0U;
    g_s_chunk_len = 0U;
    g_s_chunk_pos = 0U;
}

void cli_send(const char *p_resp)
{
    bsp_usb_cdc_write((const uint8_t *) p_resp, strlen(p_resp));
}

void cli_process(void)
{
    /* Если в g_s_chunk_buf есть остаток от предыдущего вызова —
     * продолжаем его обработку. Иначе читаем новый chunk.
     *
     * Это решает проблему потери confirm при блокирующем dispatch_test:
     * confirm приходит в chunk вместе с командой run, но process_line()
     * уходит в блокировку не дочитав chunk до конца. При следующем вызове
     * cli_process() (из inner polling loop в test_runner_wait_confirm)
     * мы продолжаем обработку остатка chunk, уже после arm_confirm().
     */
    if (g_s_chunk_pos >= g_s_chunk_len)
    {
        g_s_chunk_len = bsp_usb_cdc_read(g_s_chunk_buf, sizeof(g_s_chunk_buf));
        g_s_chunk_pos = 0U;
    }

    while (g_s_chunk_pos < g_s_chunk_len)
    {
        uint8_t byte = g_s_chunk_buf[g_s_chunk_pos];
        g_s_chunk_pos++;

        if (g_s_line_len >= (CLI_LINE_BUF_SIZE - 1U))
        {
            g_s_line_len = 0U;
            protocol_send_error("LINE_TOO_LONG");
            return;
        }

        if (byte == (uint8_t) '\n')
        {
            if (g_s_line_len > 0U && g_s_line_buf[g_s_line_len - 1U] == (uint8_t) '\r')
            {
                g_s_line_len--;
            }
            g_s_line_buf[g_s_line_len] = '\0';
            size_t completed_len       = g_s_line_len;
            g_s_line_len               = 0U; /* ← сбросить ДО process_line */
            if (completed_len > 0U)
            {
                process_line((const char *) g_s_line_buf);
            }
        }

        g_s_line_buf[g_s_line_len] = byte;
        g_s_line_len++;
    }
}
/**
 * @file  cli.c
 * @brief IO-слой и диспатчер сообщений для bootloader.
 *
 * Транспорт: USB CDC ACM через bsp_usb_cdc.
 *
 * Парсинг минималистичный: strstr по фиксированным полям (тот же подход,
 * что и в firmware_test/src/cli.c) — cJSON не используется намеренно, схема
 * входящих сообщений фиксирована.
 *
 * Входящие типы (Фаза 1):
 *   "cmd" → handle_cmd() → protocol_send_pong() / protocol_send_version_response()
 *
 * Добавление новой команды типа "cmd":
 *   1. Добавить ветку if (strcmp(cmd_name, "FOO") == 0) в handle_cmd().
 */

#include "cli.h"

#include "bsp/usb_cdc.h"
#include "protocol.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ── Ключи полей JSON ──────────────────────────────────────────────────── */

static const char K_FIELD_TYPE[] = "\"type\"";
static const char K_FIELD_CMD[]  = "\"cmd\"";

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

/* ── Обработчики входящих сообщений ────────────────────────────────────── */

/**
 * @brief Обработать сообщение {"type":"cmd",...}.
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

    if (strcmp(cmd_name, "get_version") == 0)
    {
        protocol_send_version_response();
        return;
    }

    protocol_send_error("UNKNOWN_CMD");
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

/**
 * @file  cli.c
 * @brief Реализация CLI для firmware_test.
 *
 * Транспорт: USB CDC ACM через bsp_usb_cdc — единственный канал.
 * Парсинг JSON минималистичный: strstr по полю "cmd".
 * Полноценный JSON-парсер (cJSON) не используется намеренно —
 * схема фиксирована, единственное входящее поле — "cmd".
 *
 * Добавление новой команды:
 *   1. Объявить static void cmd_foo(void); выше таблицы.
 *   2. Добавить { "FOO", cmd_foo } в k_cmds[].
 *   3. Реализовать обработчик ниже раздела "Command handlers".
 */

#include "cli.h"

#include "bsp/usb_cdc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ── Forward declarations ──────────────────────────────────────────────── */

static void cmd_ping(void);

/* ── Command table ─────────────────────────────────────────────────────── */

typedef void (*cli_handler_t)(void);

typedef struct
{
    const char *name;
    cli_handler_t handler;
} cli_cmd_t;

static const cli_cmd_t k_cmds[] = {
    { "PING", cmd_ping },
};

#define CLI_CMD_COUNT (sizeof(k_cmds) / sizeof(k_cmds[0]))

/* ── RX line buffer ────────────────────────────────────────────────────── */

static uint8_t g_s_line_buf[CLI_LINE_BUF_SIZE];
static size_t g_s_line_len = 0U;

/* ── Internal helpers ──────────────────────────────────────────────────── */

/**
 * @brief Извлечь значение поля "cmd" из JSON-строки.
 *
 * Ищет паттерн ` "cmd":"<ASCII без кавычек и обратных слэшей>"\n`. Без рекурсии и динамической памяти.
 *
 * @param[in]  line      NULL-terminated входная строка.
 * @param[out] out       Буфер для записи значения.
 * @param[in]  out_size  Размер out (включая место под '\0').
 * @return true если поле найдено и значение помещается в out.
 */
static bool parse_cmd_field(const char *p_line, char *p_out, size_t out_size)
{
    const char *key = strstr(p_line, "\"cmd\"");
    if (key == NULL)
    {
        return false;
    }

    const char *colon = strchr(key + 5U, ':');
    if (colon == NULL)
    {
        return false;
    }

    const char *open_q = strchr(colon + 1U, '"');
    if (open_q == NULL)
    {
        return false;
    }
    open_q++; /* skip the opening quote */

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
 * @brief Найти и вызвать обработчик команды.
 *
 * Если команда не найдена — отправить UNKNOWN_CMD.
 */
static void dispatch(const char *p_cmd_name)
{
    for (size_t i = 0U; i < CLI_CMD_COUNT; i++)
    {
        if (strcmp(k_cmds[i].name, p_cmd_name) == 0)
        {
            k_cmds[i].handler();
            return;
        }
    }
    cli_send("{\"ok\":false,\"error\":\"UNKNOWN_CMD\"}\n");
}

/**
 * @brief Обработать одну накопленную строку (без завершающего '\n').
 */
static void process_line(const char *p_line)
{

    const uint8_t MAX_CMD_LEN = 32U;
    char cmd_name[MAX_CMD_LEN];

    if (!parse_cmd_field(p_line, cmd_name, sizeof(cmd_name)))
    {
        cli_send("{\"ok\":false,\"error\":\"PARSE_ERR\"}\n");
        return;
    }

    dispatch(cmd_name);
}

/* ── Public API ────────────────────────────────────────────────────────── */

void cli_init(void)
{
    g_s_line_len = 0U;
}

void cli_send(const char *p_resp)
{
    bsp_usb_cdc_write((const uint8_t *) p_resp, strlen(p_resp));
}

void cli_process(void)
{
    uint8_t chunk[CLI_LINE_BUF_SIZE];

    size_t nbytes = bsp_usb_cdc_read(chunk, sizeof(chunk));

    for (size_t byte_idx = 0U; byte_idx < nbytes; byte_idx++)
    {
        uint8_t byte = chunk[byte_idx];

        if (g_s_line_len >= (CLI_LINE_BUF_SIZE - 1U))
        {
            g_s_line_len = 0U;
            cli_send("{\"ok\":false,\"error\":\"LINE_TOO_LONG\"}\n");
            continue;
        }

        if (byte == (uint8_t) '\n')
        {
            /* Отбросить '\r' если терминал шлёт CR+LF */
            if (g_s_line_len > 0U && g_s_line_buf[g_s_line_len - 1U] == (uint8_t) '\r')
            {
                g_s_line_len--;
            }
            g_s_line_buf[g_s_line_len] = '\0';
            if (g_s_line_len > 0U)
            {
                process_line((const char *) g_s_line_buf);
            }
            g_s_line_len = 0U;
        }
        else
        {
            g_s_line_buf[g_s_line_len] = byte;
            g_s_line_len++;
        }
    }
}

/* ── Command handlers ──────────────────────────────────────────────────── */

static void cmd_ping(void)
{
    cli_send("{\"ok\":true,\"result\":\"PONG\"}\n");
}
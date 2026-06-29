/**
 * @file  protocol.c
 * @brief Протокол firmware_test v2 — реализация сериализации.
 */

#include "protocol.h"

#include "bsp/tick.h"
#include "cli.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
/* ── Константы ─────────────────────────────────────────────────────────── */

/**
 * @brief Размер внутреннего TX-буфера.
 *
 * Должен вмещать самое длинное сообщение:
 * test_result c detail[96] ≈ 185 байт, берём с запасом.
 */
#define PROTO_BUF_SIZE 256U

/* ── Вспомогательные функции ───────────────────────────────────────────── */

/**
 * @brief Преобразовать статус теста в строку для JSON.
 *
 * @param[in] status  Статус теста.
 * @return Строковое представление статуса.
 */
static const char *status_to_str(test_status_t status)
{
    switch (status)
    {
    case TEST_STATUS_PASS:
        return "pass";
    case TEST_STATUS_FAIL:
        return "fail";
    case TEST_STATUS_SKIP:
        return "skip";
    default:
        return "unknown";
    }
}

/* ── Public API ────────────────────────────────────────────────────────── */

void protocol_send_session_start(void)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"session_start\","
                    "\"fw\":\"" FIRMWARE_TEST_VERSION "\","
                    "\"target\":\"IMXRT1052\","
                    "\"uptime_ms\":%" PRIu32 "}\n",
                    bsp_tick_get_ms());
    cli_send(buf);
}

void protocol_send_test_begin(const test_module_t *p_mod)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"test_begin\","
                    "\"id\":\"%s\","
                    "\"name\":\"%s\","
                    "\"critical\":%s}\n",
                    p_mod->id, p_mod->name, p_mod->critical ? "true" : "false");
    cli_send(buf);
}

void protocol_send_test_result(const test_module_t *p_mod, const test_result_t *p_result)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"test_result\","
                    "\"id\":\"%s\","
                    "\"status\":\"%s\","
                    "\"ms\":%" PRIu32 ","
                    "\"detail\":\"%s\"}\n",
                    p_mod->id, status_to_str(p_result->status), p_result->duration_ms,
                    p_result->detail);
    cli_send(buf);
}

void protocol_send_summary(uint8_t passed, uint8_t failed, uint8_t skipped, bool overall_pass)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"summary\","
                    "\"passed\":%u,"
                    "\"failed\":%u,"
                    "\"skipped\":%u,"
                    "\"overall\":\"%s\"}\n",
                    (unsigned int) passed, (unsigned int) failed, (unsigned int) skipped,
                    overall_pass ? "pass" : "fail");
    cli_send(buf);
}

void protocol_send_confirm_request(const confirm_params_t *p_params)
{
    const uint32_t EFFECTIVE_TIMEOUT =
        (p_params->timeout_ms == 0U) ? PROTOCOL_CONFIRM_TIMEOUT_MS : p_params->timeout_ms;

    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"confirm_request\","
                    "\"id\":\"%s\","
                    "\"prompt\":\"%s\","
                    "\"timeout_ms\":%" PRIu32 "}\n",
                    p_params->id, p_params->prompt, EFFECTIVE_TIMEOUT);
    cli_send(buf);
}

void protocol_send_test_list(const test_module_t *const *p_pp_registry, size_t count)
{
    /* Заголовок массива */
    cli_send("{\"type\":\"test_list\",\"tests\":[");

    for (size_t i = 0U; i < count; i++)
    {
        const test_module_t *mod = p_pp_registry[i];
        char buf[PROTO_BUF_SIZE];
        (void) snprintf(buf, sizeof(buf),
                        "{\"id\":\"%s\","
                        "\"name\":\"%s\","
                        "\"critical\":%s,"
                        "\"requires_hil\":%s}%s",
                        mod->id, mod->name, mod->critical ? "true" : "false",
                        mod->requires_hil ? "true" : "false", (i + 1U < count) ? "," : "");
        cli_send(buf);
    }

    cli_send("]}\n");
}

void protocol_send_pong(void)
{
    cli_send("{\"type\":\"pong\"}\n");
}

void protocol_send_error(const char *p_code)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}\n", p_code);
    cli_send(buf);
}

void protocol_send_uid_response(const uint8_t *p_uid)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"uid_response\","
                    "\"uid\":\"%02X%02X%02X%02X%02X%02X%02X%02X\"}\n",
                    (unsigned int) p_uid[0U], (unsigned int) p_uid[1U], (unsigned int) p_uid[2U],
                    (unsigned int) p_uid[3U], (unsigned int) p_uid[4U], (unsigned int) p_uid[5U],
                    (unsigned int) p_uid[6U], (unsigned int) p_uid[7U]);
    cli_send(buf);
}
/**
 * @file  protocol.c
 * @brief Протокол bootloader — реализация сериализации.
 */

#include "protocol.h"

#include "bsp/boot_state.h"
#include "bsp/wdog.h"
#include "cli.h"
#include "recovery.h"

#include <stdio.h>

/* ── Константы ─────────────────────────────────────────────────────────── */

/** @brief Размер внутреннего TX-буфера. */
#define PROTO_BUF_SIZE 128U

/* ── Public API ────────────────────────────────────────────────────────── */

void protocol_send_pong(void)
{
    cli_send("{\"type\":\"pong\"}\n");
}

void protocol_send_version_response(void)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"version_response\","
                    "\"fw\":\"" BOOTLOADER_VERSION "\"}\n");
    cli_send(buf);
}

void protocol_send_error(const char *p_code)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}\n", p_code);
    cli_send(buf);
}

void protocol_send_status(const char *p_state)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf), "{\"type\":\"status\",\"state\":\"%s\"}\n", p_state);
    cli_send(buf);
}

void protocol_send_wdog_status(void)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"wdog\",\"armed\":%s,\"timeout_s\":%u,\"recovered\":%s,"
                    "\"reset_count\":%u,\"threshold\":%u}\n",
                    bsp_wdog_is_armed() ? "true" : "false",
                    (unsigned) bsp_wdog_timeout_s(),
                    bsp_wdog_caused_last_reset() ? "true" : "false",
                    (unsigned) bsp_boot_attempt_count(),
                    (unsigned) RECOVERY_DEFAULT_THRESHOLD);
    cli_send(buf);
}

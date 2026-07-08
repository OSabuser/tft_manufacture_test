/**
 * @file  protocol.c
 * @brief Протокол bootloader — реализация сериализации.
 */

#include "protocol.h"

#include "cli.h"

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

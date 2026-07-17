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

#ifdef BOOTLOADER_DEV_DIAGNOSTICS
#include <inttypes.h>
#endif

/* ── Константы ─────────────────────────────────────────────────────────── */

/**
 * @brief Размер внутреннего TX-буфера.
 *
 * 192, не 128: {"type":"sdram_test",...} с именем фазы "sequential" и
 * fail_addr/expected/got — самое длинное сообщение протокола, ~140 байт.
 */
#define PROTO_BUF_SIZE 192U

/* ── Состояние модуля ──────────────────────────────────────────────────── */

/** @brief Кэш результата smoke-теста SDRAM/SEMC — см. protocol_send_smoke_status(). */
static bool s_smoke_result_known = false;
static bool s_smoke_result_pass  = false;

/** @brief Кэш информации о QSPI-чипе — см. protocol_send_qspi_info(). */
static bool s_qspi_info_known      = false;
static bool s_qspi_pass            = false;
static uint8_t s_qspi_mfr_id       = 0U;
static uint8_t s_qspi_cap_byte     = 0U;
static uint32_t s_qspi_size_mb     = 0U;
static const char *s_p_qspi_chip   = ""; /* строковый литерал из bsp_qspi_decode_chip() — статичен */

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

void protocol_set_smoke_result(bool pass)
{
    s_smoke_result_known = true;
    s_smoke_result_pass  = pass;
}

void protocol_send_smoke_status(void)
{
    if (s_smoke_result_known)
    {
        protocol_send_status(s_smoke_result_pass ? "smoke_pass" : "smoke_fail");
    }
}

void protocol_set_qspi_info(uint8_t mfr_id, const char *p_chip_name, uint8_t cap_byte, uint32_t size_mb, bool pass)
{
    s_qspi_info_known = true;
    s_qspi_mfr_id      = mfr_id;
    s_p_qspi_chip      = p_chip_name;
    s_qspi_cap_byte    = cap_byte;
    s_qspi_size_mb     = size_mb;
    s_qspi_pass        = pass;
}

void protocol_send_qspi_info(void)
{
    if (!s_qspi_info_known)
    {
        return;
    }

    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"qspi_info\",\"chip\":\"%s\",\"mfr\":\"0x%02X\","
                    "\"cap_byte\":\"0x%02X\",\"size_mb\":%u,\"pass\":%s}\n",
                    s_p_qspi_chip, (unsigned) s_qspi_mfr_id, (unsigned) s_qspi_cap_byte,
                    (unsigned) s_qspi_size_mb, s_qspi_pass ? "true" : "false");
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

#ifdef BOOTLOADER_DEV_DIAGNOSTICS
void protocol_send_sdram_test_phase(
    const char *p_phase, bool pass, uint32_t duration_ms, uint32_t fail_addr, uint8_t expected, uint8_t got)
{
    char buf[PROTO_BUF_SIZE];
    (void) snprintf(buf, sizeof(buf),
                    "{\"type\":\"sdram_test\",\"phase\":\"%s\",\"pass\":%s,\"duration_ms\":%" PRIu32
                    ",\"fail_addr\":\"0x%08" PRIX32 "\",\"expected\":\"0x%02X\",\"got\":\"0x%02X\"}\n",
                    p_phase, pass ? "true" : "false", duration_ms, fail_addr, (unsigned) expected, (unsigned) got);
    cli_send(buf);
}
#endif /* BOOTLOADER_DEV_DIAGNOSTICS */

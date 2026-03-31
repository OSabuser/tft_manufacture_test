/**
 * @file  tests/target/hil_can/main.c
 * @brief HIL target — CLI для тестирования bsp_can.
 *
 * Протокол: текстовые команды через LPUART1 (MCU-Link VCOM), \r\n-terminated.
 *
 * Команды:
 *   PING                            -> PONG
 *   CAN_SEND <id> <ext> <dlc> <b0>..<b7>
 *                                   -> OK / ERR_TIMEOUT / ERR_BUSY / ERR_PARAM
 *   CAN_RECV <timeout_ms>           -> <id> <ext> <dlc> <b0>..<b7> / TIMEOUT
 *   CAN_FILTER <idx> <id> <mask> <ext>
 *                                   -> OK / ERR_PARAM
 *   CAN_ACCEPT_ALL                  -> OK
 *   CAN_RX_EVENTS                   -> <число>
 *   CAN_LAST_RX                     -> <id> <ext> <dlc> <b0>..<b7>
 *   CAN_RESET_EVENTS                -> OK
 *
 * Формат числовых параметров: десятичный (для простоты парсинга).
 * ext: 0 = STD, 1 = EXT.
 *
 * Стенд:
 *   M5StampPLC (SIT1044 трансивер) ↔ SN65HVD230D таргета
 *   Общая CAN-шина 125 kbit/s (CAN_BITRATE в прошивке = M5 can_baud в agent.py).
 *
 * ВАЖНО:
 *   bsp_can_init() использует disableSelfReception=true — таргет не слышит
 *   собственные фреймы. Для round-trip теста «таргет TX → таргет RX» нужен
 *   второй узел (M5) на шине, который ретранслирует фрейм обратно.
 *   Для теста «M5 TX → таргет RX» второй узел не нужен.
 */

#include "board.h"
#include "bsp/can.h"
#include "bsp/led.h"
#include "bsp/tick.h"
#include "bsp/uart_host.h"

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

#define CLI_BAUD_RATE  115200U
#define CLI_LINE_MAX   256U
#define CLI_RX_TIMEOUT 50U /* мс */

#define CAN_BITRATE    125000U
#define CAN_TX_TIMEOUT 100U /* мс — таймаут отправки одного фрейма */

/* -------------------------------------------------------------------------- */
/* Event tracking                                                             */
/* -------------------------------------------------------------------------- */

static volatile uint32_t s_rx_event_count;
static volatile bsp_can_frame_t s_last_rx_frame;

/* -------------------------------------------------------------------------- */
/* CLI helpers                                                                */
/* -------------------------------------------------------------------------- */

static size_t cli_read_line(uint8_t *p_buf, size_t max_len)
{
    size_t pos = 0U;

    while (pos < (max_len - 1U))
    {
        int32_t byte = bsp_uart_host_read_byte(CLI_RX_TIMEOUT);
        if (byte < 0)
        {
            break;
        }
        if ((char) byte == '\r')
        {
            continue;
        }
        if ((char) byte == '\n')
        {
            break;
        }
        p_buf[pos++] = (uint8_t) byte;
    }

    p_buf[pos] = '\0';
    return pos;
}

/** Сериализовать фрейм в строку "<id> <ext> <dlc> <b0> ... <bN>". */
static void frame_to_str(const bsp_can_frame_t *p_frame, char *p_buf, size_t buf_len)
{
    int written = snprintf(p_buf, buf_len, "%lu %u %u", (unsigned long) p_frame->id,
                           (unsigned) p_frame->is_extended, (unsigned) p_frame->dlc);

    for (uint8_t i = 0U; i < p_frame->dlc && i < BSP_CAN_DATA_MAX_LEN; i++)
    {
        int n = snprintf(p_buf + written, buf_len - (size_t) written, " %u",
                         (unsigned) p_frame->data[i]);
        if (n > 0)
        {
            written += n;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* CLI command handlers                                                       */
/* -------------------------------------------------------------------------- */

/**
 * CAN_SEND <id> <ext> <dlc> [<b0> .. <b7>]
 *
 * Пример: CAN_SEND 123 0 3 0xDE 0xAD 0xBE
 *         CAN_SEND 536870911 1 2 0xAA 0xBB
 */
static void cmd_can_send(const char *p_args)
{
    unsigned long id = 0U;
    unsigned ext     = 0U;
    unsigned dlc     = 0U;

    int parsed = sscanf(p_args, "%lu %u %u", &id, &ext, &dlc);
    if (parsed != 3 || dlc > BSP_CAN_DATA_MAX_LEN)
    {
        bsp_uart_host_write_str("ERR_PARAM\r\n");
        return;
    }

    bsp_can_frame_t frame;
    (void) memset(&frame, 0, sizeof(frame));
    frame.id          = (uint32_t) id;
    frame.is_extended = (ext != 0U);
    frame.dlc         = (uint8_t) dlc;

    /* Парсим байты данных после трёх обязательных аргументов. */
    const char *p = p_args;
    /* Пропустить id, ext, dlc. */
    for (int i = 0; i < 3; i++)
    {
        while (*p == ' ')
            p++;
        while (*p != ' ' && *p != '\0')
            p++;
    }

    for (uint8_t i = 0U; i < dlc; i++)
    {
        unsigned byte_val = 0U;
        if (sscanf(p, " %u", &byte_val) != 1)
        {
            break;
        }
        frame.data[i] = (uint8_t) byte_val;
        /* Сдвинуться на следующий аргумент. */
        while (*p == ' ')
            p++;
        while (*p != ' ' && *p != '\0')
            p++;
    }

    bsp_status_t status = bsp_can_send(&frame, CAN_TX_TIMEOUT);

    switch (status)
    {
    case BSP_OK:
        bsp_uart_host_write_str("OK\r\n");
        break;
    case BSP_ERR_TIMEOUT:
        bsp_uart_host_write_str("ERR_TIMEOUT\r\n");
        break;
    case BSP_ERR_BUSY:
        bsp_uart_host_write_str("ERR_BUSY\r\n");
        break;
    default:
        bsp_uart_host_write_str("ERR_PARAM\r\n");
        break;
    }
}

/**
 * CAN_RECV <timeout_ms>
 *
 * Блокируется до приёма фрейма или таймаута.
 * Ответ: "<id> <ext> <dlc> <b0> ... <bN>" или "TIMEOUT".
 */
static void cmd_can_recv(const char *p_args)
{
    unsigned timeout_ms = 0U;
    if (sscanf(p_args, "%u", &timeout_ms) != 1)
    {
        bsp_uart_host_write_str("ERR_PARAM\r\n");
        return;
    }

    bsp_can_frame_t frame;
    bsp_status_t status = bsp_can_receive(&frame, timeout_ms);

    if (status != BSP_OK)
    {
        bsp_uart_host_write_str("TIMEOUT\r\n");
        return;
    }

    /* Обновить счётчик событий (polling-режим — обновляем здесь). */
    s_rx_event_count++;
    s_last_rx_frame = frame;

    char resp[128];
    frame_to_str(&frame, resp, sizeof(resp) - 2U);
    (void) strncat(resp, "\r\n", sizeof(resp) - strlen(resp) - 1U);
    bsp_uart_host_write_str(resp);
}

/**
 * CAN_FILTER <idx> <id> <mask> <ext>
 *
 * Пример: CAN_FILTER 0 100 2047 0    (STD ID=100, маска=все биты, STD)
 *         CAN_FILTER 1 536870912 536870911 1  (EXT)
 */
static void cmd_can_filter(const char *p_args)
{
    unsigned idx       = 0U;
    unsigned long id   = 0U;
    unsigned long mask = 0U;
    unsigned ext       = 0U;

    if (sscanf(p_args, "%u %lu %lu %u", &idx, &id, &mask, &ext) != 4)
    {
        bsp_uart_host_write_str("ERR_PARAM\r\n");
        return;
    }

    bsp_status_t status =
        bsp_can_set_filter((uint8_t) idx, (uint32_t) id, (uint32_t) mask, (ext != 0U));

    bsp_uart_host_write_str(status == BSP_OK ? "OK\r\n" : "ERR_PARAM\r\n");
}

/* -------------------------------------------------------------------------- */
/* Main CLI dispatcher                                                        */
/* -------------------------------------------------------------------------- */

static void cli_process_line(const char *p_line)
{
    char resp[128];

    if (strncmp(p_line, "PING", 4U) == 0)
    {
        bsp_uart_host_write_str("PONG\r\n");
    }
    else if (strncmp(p_line, "CAN_SEND ", 9U) == 0)
    {
        cmd_can_send(p_line + 9U);
    }
    else if (strncmp(p_line, "CAN_RECV ", 9U) == 0)
    {
        cmd_can_recv(p_line + 9U);
    }
    else if (strncmp(p_line, "CAN_FILTER ", 11U) == 0)
    {
        cmd_can_filter(p_line + 11U);
    }
    else if (strncmp(p_line, "CAN_ACCEPT_ALL", 14U) == 0)
    {
        bsp_status_t s = bsp_can_accept_all();
        bsp_uart_host_write_str(s == BSP_OK ? "OK\r\n" : "ERR_PARAM\r\n");
    }
    else if (strncmp(p_line, "CAN_RX_EVENTS", 13U) == 0)
    {
        snprintf(resp, sizeof(resp), "%lu\r\n", (unsigned long) s_rx_event_count);
        bsp_uart_host_write_str(resp);
    }
    else if (strncmp(p_line, "CAN_LAST_RX", 11U) == 0)
    {
        frame_to_str((const bsp_can_frame_t *) &s_last_rx_frame, resp, sizeof(resp) - 2U);
        (void) strncat(resp, "\r\n", sizeof(resp) - strlen(resp) - 1U);
        bsp_uart_host_write_str(resp);
    }
    else if (strncmp(p_line, "CAN_RESET_EVENTS", 16U) == 0)
    {
        s_rx_event_count = 0U;
        (void) memset((void *) &s_last_rx_frame, 0, sizeof(s_last_rx_frame));
        bsp_uart_host_write_str("OK\r\n");
    }
    else if (p_line[0] != '\0')
    {
        bsp_uart_host_write_str("ERR_UNKNOWN\r\n");
    }
}

/* -------------------------------------------------------------------------- */
/* main                                                                       */
/* -------------------------------------------------------------------------- */

int main(void)
{
    board_hw_init();
    bsp_tick_init();
    bsp_led_init();
    bsp_uart_host_init(CLI_BAUD_RATE);

    /* --- CAN init: 125 kbit/s (совпадает с can_baud агента M5) --- */
    bsp_can_config_t can_cfg = { .bitrate = CAN_BITRATE };
    (void) bsp_can_init(&can_cfg);

    /* По умолчанию принимаем все фреймы. */
    (void) bsp_can_accept_all();

    bsp_led_on(LED_HEARTBEAT);

    /* Шлём READY пока хост не подключится. */
    while (bsp_uart_host_rx_available() == 0U)
    {
        bsp_uart_host_write_str("READY\r\n");
        bsp_led_toggle(LED_APP);
        bsp_delay(200U);
    }

    bsp_led_off(LED_APP);

    static uint8_t s_line_buf[CLI_LINE_MAX];

    for (;;)
    {
        size_t len = cli_read_line(s_line_buf, sizeof(s_line_buf));
        if (len > 0U)
        {
            cli_process_line((const char *) s_line_buf);
        }
    }

    return 0;
}
/**
 * @file  log_uart.c
 * @brief UART-адаптер логгера.
 *
 * Подключает utils_log к bsp_uart_host:
 *   - write callback → bsp_uart_host_write()
 *   - timestamp hook → bsp_tick_get_ms()
 */

#include "port/log_uart.h"

#include "bsp/tick.h"
#include "bsp/uart_host.h"
#include "log/log.h"

/* -------------------------------------------------------------------------- */
/* Strong-реализация weak-хука timestamp                                       */
/* -------------------------------------------------------------------------- */

uint32_t log_get_timestamp_ms(void)
{
    return bsp_tick_get_ms();
}

/* -------------------------------------------------------------------------- */
/* Write callback                                                               */
/* -------------------------------------------------------------------------- */

static void uart_write(const char *p_buf, size_t len, void *p_ctx)
{
    (void) p_ctx;
    (void) bsp_uart_host_write((const uint8_t *) p_buf, len);
}

/* -------------------------------------------------------------------------- */
/* Публичный API                                                                */
/* -------------------------------------------------------------------------- */

void log_uart_init(void)
{
    log_init(uart_write, NULL);
}
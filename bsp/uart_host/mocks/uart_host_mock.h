/**
 * @file  uart_host_mock.h
 * @brief Объявления fff-заглушек bsp_uart_host.
 *
 * Включать только в test-файлах, не в продакшн-коде.
 *
 * Порядок include в тест-файле:
 *   1. fff.h
 *   2. DEFINE_FFF_GLOBALS;
 *   3. bsp/uart_host.h   ← публичный API (сигнатуры функций)
 *   4. uart_host_mock.h  ← FAKE_* структуры
 */

#ifndef UART_HOST_MOCK_H
#define UART_HOST_MOCK_H

#include "bsp/status.h"
#include "bsp/uart_host.h"
#include "fff.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Объявления заглушек (DEFINE_ живут в uart_host_mock.c)                      */
/* -------------------------------------------------------------------------- */

DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_uart_host_init, uint32_t);
DECLARE_FAKE_VOID_FUNC(bsp_uart_host_deinit);

DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_uart_host_write, const uint8_t *, size_t);
DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_uart_host_write_str, const char *);

DECLARE_FAKE_VALUE_FUNC(size_t, bsp_uart_host_read, uint8_t *, size_t, uint32_t);
DECLARE_FAKE_VALUE_FUNC(int32_t, bsp_uart_host_read_byte, uint32_t);
DECLARE_FAKE_VALUE_FUNC(size_t, bsp_uart_host_rx_available);
DECLARE_FAKE_VOID_FUNC(bsp_uart_host_rx_flush);

/* -------------------------------------------------------------------------- */
/* Удобный макрос: сброс всех заглушек в setUp()                               */
/* -------------------------------------------------------------------------- */

/* clang-format off */
#define UART_HOST_MOCK_RESET_ALL()          \
    RESET_FAKE(bsp_uart_host_init);         \
    RESET_FAKE(bsp_uart_host_deinit);       \
    RESET_FAKE(bsp_uart_host_write);        \
    RESET_FAKE(bsp_uart_host_write_str);    \
    RESET_FAKE(bsp_uart_host_read);         \
    RESET_FAKE(bsp_uart_host_read_byte);    \
    RESET_FAKE(bsp_uart_host_rx_available); \
    RESET_FAKE(bsp_uart_host_rx_flush)
/* clang-format on */

#endif /* UART_HOST_MOCK_H */
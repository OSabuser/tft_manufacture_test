/**
 * @file  uart_host_mock.c
 * @brief fff-заглушки bsp_uart_host для host unit-тестов (Humble Object).
 *
 * Подключается вместо uart_host.c при BUILD_TESTS_HOST.
 * Не требует SDK, LPUART, прерываний.
 *
 * Использование в тесте:
 *
 *   #include "fff.h"
 *   DEFINE_FFF_GLOBALS;
 *
 *   #include "bsp/uart_host.h"
 *   #include "uart_host_mock.h"   // объявления FAKE_*
 *
 *   void setUp(void)   { UART_HOST_MOCK_RESET_ALL(); }
 *   void tearDown(void) {}
 *
 *   void test_something(void) {
 *       bsp_uart_host_write_fake.return_val = BSP_OK;
 *       // ... вызываем код под тестом ...
 *       TEST_ASSERT_EQUAL(1, bsp_uart_host_write_fake.call_count);
 *   }
 */

#include "uart_host_mock.h"

/* -------------------------------------------------------------------------- */
/* Определения fff-заглушек                                                    */
/* -------------------------------------------------------------------------- */

DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_uart_host_init, uint32_t);
DEFINE_FAKE_VOID_FUNC(bsp_uart_host_deinit);

DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_uart_host_write, const uint8_t *, size_t);
DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_uart_host_write_str, const char *);

DEFINE_FAKE_VALUE_FUNC(size_t, bsp_uart_host_read, uint8_t *, size_t, uint32_t);
DEFINE_FAKE_VALUE_FUNC(int32_t, bsp_uart_host_read_byte, uint32_t);
DEFINE_FAKE_VALUE_FUNC(size_t, bsp_uart_host_rx_available);
DEFINE_FAKE_VOID_FUNC(bsp_uart_host_rx_flush);
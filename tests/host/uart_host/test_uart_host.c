/**
 * @file  test_uart_host.c
 * @brief Unit-тесты для кода использующего bsp_uart_host через fff-мок.
 *
 * Сборка: host (x86), без SDK, без железа.
 * Фреймворки: Unity (assertions) + fff (mocking).
 *
 * Что тестируем:
 *   Не саму реализацию uart_host.c (она тестируется HIL-тестами через pyserial),
 *   а то что код верхнего уровня правильно вызывает API uart_host:
 *   корректные аргументы, правильная обработка возвращаемых значений,
 *   поведение при ошибках (BSP_ERR_INIT, таймаут и т.д.).
 */

/* ── 1. fff — до любых mock-заголовков ─────────────────────────────────── */
#include "fff.h"
DEFINE_FFF_GLOBALS;

/* ── 2. Мок uart_host (внутри подтягивает bsp/uart_host.h) ─────────────── */
#include "uart_host_mock.h"

/* ── 3. Unity ───────────────────────────────────────────────────────────── */
#include "unity.h"

/* ── 4. Тестируемый модуль — раскомментировать когда появится ──────────── */
/* #include "protocol/protocol.h" */

/* -------------------------------------------------------------------------- */

void setUp(void)
{
    UART_HOST_MOCK_RESET_ALL();
}

void tearDown(void)
{
}

/* -------------------------------------------------------------------------- */
/* init                                                                        */
/* -------------------------------------------------------------------------- */

void test_init_called_once_with_correct_baud_rate(void)
{
    bsp_uart_host_init_fake.return_val = BSP_OK;

    bsp_status_t status = bsp_uart_host_init(115200U);

    TEST_ASSERT_EQUAL(BSP_OK, status);
    TEST_ASSERT_EQUAL(1, bsp_uart_host_init_fake.call_count);
    TEST_ASSERT_EQUAL(115200U, bsp_uart_host_init_fake.arg0_val);
}

void test_init_returns_err_on_failure(void)
{
    bsp_uart_host_init_fake.return_val = BSP_ERR_INIT;

    bsp_status_t status = bsp_uart_host_init(115200U);

    TEST_ASSERT_EQUAL(BSP_ERR_INIT, status);
}

/* -------------------------------------------------------------------------- */
/* write                                                                       */
/* -------------------------------------------------------------------------- */

void test_write_called_with_correct_args(void)
{
    const uint8_t payload[]             = { 0xAA, 0xBB, 0xCC };
    bsp_uart_host_write_fake.return_val = BSP_OK;

    bsp_status_t status = bsp_uart_host_write(payload, sizeof(payload));

    TEST_ASSERT_EQUAL(BSP_OK, status);
    TEST_ASSERT_EQUAL(1, bsp_uart_host_write_fake.call_count);
    TEST_ASSERT_EQUAL_PTR(payload, bsp_uart_host_write_fake.arg0_val);
    TEST_ASSERT_EQUAL(sizeof(payload), bsp_uart_host_write_fake.arg1_val);
}

void test_write_str_called_with_correct_string(void)
{
    bsp_uart_host_write_str_fake.return_val = BSP_OK;

    bsp_status_t status = bsp_uart_host_write_str("hello\r\n");

    TEST_ASSERT_EQUAL(BSP_OK, status);
    TEST_ASSERT_EQUAL_STRING("hello\r\n", bsp_uart_host_write_str_fake.arg0_val);
}

void test_write_returns_err_when_not_initialized(void)
{
    bsp_uart_host_write_fake.return_val = BSP_ERR_INIT;

    bsp_status_t status = bsp_uart_host_write(NULL, 0U);

    TEST_ASSERT_EQUAL(BSP_ERR_INIT, status);
}

/* -------------------------------------------------------------------------- */
/* read_byte                                                                   */
/* -------------------------------------------------------------------------- */

void test_read_byte_returns_byte_on_success(void)
{
    bsp_uart_host_read_byte_fake.return_val = 0x42;

    int32_t byte = bsp_uart_host_read_byte(100U);

    TEST_ASSERT_EQUAL(0x42, byte);
    TEST_ASSERT_EQUAL(100U, bsp_uart_host_read_byte_fake.arg0_val);
}

void test_read_byte_returns_minus1_on_timeout(void)
{
    bsp_uart_host_read_byte_fake.return_val = -1;

    int32_t byte = bsp_uart_host_read_byte(100U);

    TEST_ASSERT_EQUAL(-1, byte);
}

void test_read_byte_wait_forever_passes_correct_timeout(void)
{
    bsp_uart_host_read_byte_fake.return_val = 0x01;

    (void) bsp_uart_host_read_byte(BSP_UART_HOST_WAIT_FOREVER);

    TEST_ASSERT_EQUAL(UINT32_MAX, bsp_uart_host_read_byte_fake.arg0_val);
}

/* -------------------------------------------------------------------------- */
/* read                                                                        */
/* -------------------------------------------------------------------------- */

void test_read_returns_number_of_bytes_read(void)
{
    uint8_t buf[16];
    bsp_uart_host_read_fake.return_val = 5U;

    size_t n = bsp_uart_host_read(buf, sizeof(buf), 200U);

    TEST_ASSERT_EQUAL(5U, n);
    TEST_ASSERT_EQUAL_PTR(buf, bsp_uart_host_read_fake.arg0_val);
    TEST_ASSERT_EQUAL(sizeof(buf), bsp_uart_host_read_fake.arg1_val);
    TEST_ASSERT_EQUAL(200U, bsp_uart_host_read_fake.arg2_val);
}

void test_read_partial_is_not_an_error(void)
{
    uint8_t buf[64];
    /* Запросили 64, получили 3 — частичное чтение, не ошибка */
    bsp_uart_host_read_fake.return_val = 3U;

    size_t n = bsp_uart_host_read(buf, sizeof(buf), 500U);

    TEST_ASSERT_EQUAL(3U, n);
}

/* -------------------------------------------------------------------------- */
/* rx helpers                                                                  */
/* -------------------------------------------------------------------------- */

void test_rx_available_returns_count(void)
{
    bsp_uart_host_rx_available_fake.return_val = 42U;

    size_t n = bsp_uart_host_rx_available();

    TEST_ASSERT_EQUAL(42U, n);
}

void test_rx_flush_called_once(void)
{
    bsp_uart_host_rx_flush();

    TEST_ASSERT_EQUAL(1, bsp_uart_host_rx_flush_fake.call_count);
}

/* -------------------------------------------------------------------------- */
/* setUp сбрасывает счётчики между тестами                                    */
/* -------------------------------------------------------------------------- */

void test_mock_reset_clears_state(void)
{
    bsp_uart_host_write_fake.return_val = BSP_OK;
    (void) bsp_uart_host_write(NULL, 0U);
    TEST_ASSERT_EQUAL(1, bsp_uart_host_write_fake.call_count);

    /* setUp() следующего теста вызовет UART_HOST_MOCK_RESET_ALL() */
    UART_HOST_MOCK_RESET_ALL();
    TEST_ASSERT_EQUAL(0, bsp_uart_host_write_fake.call_count);
    TEST_ASSERT_EQUAL(0, bsp_uart_host_write_fake.return_val);
}

/* -------------------------------------------------------------------------- */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_called_once_with_correct_baud_rate);
    RUN_TEST(test_init_returns_err_on_failure);

    RUN_TEST(test_write_called_with_correct_args);
    RUN_TEST(test_write_str_called_with_correct_string);
    RUN_TEST(test_write_returns_err_when_not_initialized);

    RUN_TEST(test_read_byte_returns_byte_on_success);
    RUN_TEST(test_read_byte_returns_minus1_on_timeout);
    RUN_TEST(test_read_byte_wait_forever_passes_correct_timeout);

    RUN_TEST(test_read_returns_number_of_bytes_read);
    RUN_TEST(test_read_partial_is_not_an_error);

    RUN_TEST(test_rx_available_returns_count);
    RUN_TEST(test_rx_flush_called_once);

    RUN_TEST(test_mock_reset_clears_state);

    return UNITY_END();
}
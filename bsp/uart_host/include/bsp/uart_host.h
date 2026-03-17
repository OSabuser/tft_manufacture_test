/**
 * @file  uart_host.h
 * @brief BSP: коммуникационный канал с хост-машиной (LPUART1, J2).
 *
 * Назначение:
 *   - HIL-тесты (pytest + pyserial через MCU-Link VCOM)
 *   - Резервный канал связи / отладочный вывод
 *
 * Архитектура:
 *   TX  — blocking polling (LPUART_WriteBlocking)
 *   RX  — ISR → ring_buffer → polling read с таймаутом
 *
 * Паттерн singleton: один экземпляр на всё приложение.
 *
 * Размер RX-буфера задаётся через CMake define:
 *   target_compile_definitions(... PRIVATE BSP_UART_HOST_RX_BUFFER_SIZE=256)
 * Значение должно быть степенью двойки. Дефолт: 256.
 */
#ifndef BSP_UART_HOST_
#define BSP_UART_HOST_

#include "bsp/status.h"

#include <stddef.h>
#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Константы                                                                   */
/* -------------------------------------------------s------------------------- */

/** Передать в timeout_ms чтобы ждать данные бесконечно. */
#define BSP_UART_HOST_WAIT_FOREVER (UINT32_MAX)

/* -------------------------------------------------------------------------- */
/* Инициализация                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Инициализирует LPUART1: тактирование, пины, прерывание, ring buffer.
 *
 * Вызывать один раз из main() после board_hw_init().
 * Повторный вызов без предварительного deinit вернёт BSP_ERR_INIT.
 *
 * @param baud_rate  Скорость в бод, например 115200.
 * @return BSP_OK при успехе, BSP_ERR_INIT при ошибке.
 */
bsp_status_t bsp_uart_host_init(uint32_t baud_rate);

/**
 * @brief Деинициализирует LPUART1, отключает прерывание, сбрасывает буфер.
 *
 * После вызова модуль можно инициализировать повторно.
 */
void bsp_uart_host_deinit(void);

/* -------------------------------------------------------------------------- */
/* TX — blocking polling                                                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Отправляет массив байт. Блокирует до завершения передачи.
 *
 * @param p_data  Указатель на буфер данных.
 * @param len     Количество байт для отправки.
 * @return BSP_OK или BSP_ERR_INIT если модуль не инициализирован.
 */
bsp_status_t bsp_uart_host_write(const uint8_t *p_data, size_t len);

/**
 * @brief Отправляет C-строку (без нулевого терминатора).
 *
 * @param p_str  Нуль-терминированная строка.
 * @return BSP_OK или BSP_ERR_INIT если модуль не инициализирован.
 */
bsp_status_t bsp_uart_host_write_str(const char *p_str);

/* -------------------------------------------------------------------------- */
/* RX — ring buffer + polling с таймаутом                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Читает до @p len байт с таймаутом.
 *
 * Функция возвращает управление, как только:
 *   - прочитано @p len байт, ИЛИ
 *   - истёк @p timeout_ms с момента последнего поступившего байта,
 *     ИЛИ буфер пуст и @p timeout_ms == 0.
 *
 * Частичное чтение — не ошибка; caller сам проверяет возвращённое значение.
 *
 * @param p_buf       Буфер для записи принятых данных.
 * @param len         Максимальное число байт для чтения.
 * @param timeout_ms  Таймаут ожидания в мс. 0 — без ожидания,
 *                    BSP_UART_HOST_WAIT_FOREVER — ждать бесконечно.
 * @return Число фактически прочитанных байт (0..len).
 */
size_t bsp_uart_host_read(uint8_t *p_buf, size_t len, uint32_t timeout_ms);

/**
 * @brief Читает один байт с таймаутом.
 *
 * @param timeout_ms  Таймаут в мс. 0 — без ожидания,
 *                    BSP_UART_HOST_WAIT_FOREVER — ждать бесконечно.
 * @return Принятый байт (0..255) или -1 при таймауте / не инициализирован.
 */
int32_t bsp_uart_host_read_byte(uint32_t timeout_ms);

/**
 * @brief Возвращает число байт, доступных в RX-буфере прямо сейчас.
 *
 * @return Число байт (0 если буфер пуст или модуль не инициализирован).
 */
size_t bsp_uart_host_rx_available(void);

/**
 * @brief Сбрасывает содержимое RX-буфера.
 */
void bsp_uart_host_rx_flush(void);

#endif //BSP_UART_HOST_
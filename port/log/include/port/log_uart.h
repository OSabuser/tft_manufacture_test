/**
 * @file  port/log_uart.h
 * @brief UART-адаптер логгера: регистрирует bsp_uart_host как транспорт.
 *
 * Инициализация в main():
 * @code
 *   bsp_tick_init();
 *   bsp_uart_host_init(115200U);
 *   // В tft_app: log_mutex_init() здесь
 *   log_uart_init();   // регистрирует транспорт + timestamp
 * @endcode
 *
 * При смене транспорта (Flash, USB CDC) — заменить этот файл на
 * port/log_flash/log_flash.h и вызвать log_flash_init() вместо log_uart_init().
 * Код с LOG_I/LOG_D/... не меняется.
 */

#ifndef PORT_LOG_UART_H
#define PORT_LOG_UART_H

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * @brief Зарегистрировать UART как транспорт логгера.
 *
 * Вызывает log_init() с uart-callback и регистрирует
 * log_get_timestamp_ms() → bsp_tick_get_ms().
 *
 * Предусловия: bsp_uart_host_init() и bsp_tick_init() уже вызваны.
 */
    void log_uart_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_LOG_UART_H */
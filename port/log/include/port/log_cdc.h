/**
 * @file  port/log_cdc.h
 * @brief USB CDC ACM адаптер логгера: регистрирует bsp_usb_cdc как транспорт.
 *
 * @warning Инициализация — ТОЛЬКО после vTaskStartScheduler() (из задачи
 * FreeRTOS). bsp_usb_cdc_init() внутри себя вызывает bsp_delay(), которая в
 * FreeRTOS-режиме bsp_tick реализована через xTaskGetTickCount() — до старта
 * планировщика тик не идёт (счётчик стоит на 0), вызов до этого момента
 * зависает навсегда.
 *
 * @code
 *   // внутри задачи, ПОСЛЕ vTaskStartScheduler():
 *   bsp_usb_cdc_init();
 *   log_cdc_init();   // регистрирует транспорт + timestamp
 * @endcode
 *
 * Транспорт best-effort: пока хост не открыл VCOM (или предыдущая передача
 * ещё не завершена), записи молча отбрасываются — см. bsp_usb_cdc_write().
 * Не подходит для сообщений, которые обязаны быть доставлены — для этого
 * нужен ack-протокол поверх (как JSON-протокол bootloader'а).
 */

#ifndef PORT_LOG_CDC_H
#define PORT_LOG_CDC_H

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * @brief Зарегистрировать USB CDC как транспорт логгера.
 *
 * Вызывает log_init() с cdc-callback и регистрирует
 * log_get_timestamp_ms() → bsp_tick_get_ms().
 *
 * Предусловие: bsp_usb_cdc_init() уже вызван — после старта планировщика,
 * см. предупреждение в докстроке файла.
 */
    void log_cdc_init(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_LOG_CDC_H */

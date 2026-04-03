/*
 * Copyright 2025 MU LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file usb_cdc.h
 * @brief BSP: USB CDC ACM (Virtual COM Port) на USB1 / EHCI0.
 *
 * Публичный API без зависимостей от NXP SDK.
 * Все USB-специфичные типы и хедеры скрыты в реализации.
 */

#ifndef BSP_USB_CDC_H_
#define BSP_USB_CDC_H_

#include "bsp/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Максимальный размер одной передачи (HS bulk packet). */
#define BSP_USB_CDC_MAX_PACKET_SIZE (512U)

    /**
 * @brief Инициализация USB CDC ACM (USB1 / EHCI0).
 *
 * Включает тактирование USB PHY 480 MHz, инициализирует EHCI контроллер,
 * регистрирует CDC ACM дескрипторы и запускает USB device stack.
 *
 * @pre board_hw_init() вызван (MPU настроен, NonCacheable регион активен).
 *
 * @return BSP_OK при успехе, BSP_ERR_HW при ошибке  инициализациистека.
 */
    bsp_status_t bsp_usb_cdc_init(void);

    /**
 * @brief Проверить готовность канала (хост открыл COM-порт).
 *
 * Возвращает true когда USB enumeration завершён и хост установил
 * DTR (Data Terminal Ready) через SET_CONTROL_LINE_STATE.
 *
 * @return true — можно передавать данные, false — хост не подключён.
 */
    bool bsp_usb_cdc_is_ready(void);

    /**
 * @brief Отправить данные хосту (неблокирующая).
 *
 * Копирует данные во внутренний TX буфер (NonCacheable OCRAM)
 * и ставит в очередь USB IN transfer.
 *
 * @param data  Указатель на данные.
 * @param len   Количество байт, 0 < len <= BSP_USB_CDC_MAX_PACKET_SIZE.
 *
 * @return BSP_OK — transfer поставлен в очередь.
 * @return BSP_ERR_BUSY — предыдущий transfer не завершён.
 * @return BSP_ERR_NOT_READY — хост не подключён или USB не инициализирован.
 * @return BSP_ERR_INVALID — data == NULL или len == 0 или len слишком велик.
 */
    bsp_status_t bsp_usb_cdc_write(const uint8_t *p_data, size_t len);

    /**
 * @brief Готов ли TX канал к следующей отправке.
 *
 * @return true — можно вызывать bsp_usb_cdc_write().
 */
    bool bsp_usb_cdc_write_ready(void);

    /**
 * @brief Прочитать данные от хоста (неблокирующая).
 *
 * Забирает данные из внутреннего RX буфера, заполненного USB OUT
 * callback. Автоматически перепланирует следующий OUT transfer.
 *
 * @param buf      Буфер для приёма.
 * @param max_len  Размер буфера.
 *
 * @return Количество прочитанных байт (0 если данных нет).
 */
    size_t bsp_usb_cdc_read(uint8_t *p_buf, size_t max_len);

    /**
 * @brief Поллинг USB стека — вызывать из main loop (bare-metal).
 *
 * Для FreeRTOS не нужен если USB_DEVICE_CONFIG_USE_TASK = 1.
 */
    void bsp_usb_cdc_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_USB_CDC_H_ */
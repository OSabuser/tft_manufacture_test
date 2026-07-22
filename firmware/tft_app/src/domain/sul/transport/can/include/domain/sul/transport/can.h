/**
 * @file  can.h
 * @brief Тонкий транспорт-адаптер bsp_can → sul_frame_t (HW, не host-тестируется).
 *
 * Не декодирует протокол — только инициализация CAN + фильтры и перевод
 * bsp_can_frame_t в нейтральный sul_frame_t. Декодирование — domain/sul/nku_can.h.
 */

#ifndef DOMAIN_SUL_TRANSPORT_CAN_H_
#define DOMAIN_SUL_TRANSPORT_CAN_H_

#include "bsp/status.h"
#include "domain/sul.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief bsp_can_init(). RX-фильтры НЕ настраивает.
 *
 * Вызывающий обязан сразу после этого позвать sul_transport_can_set_address()
 * — внутренний сентинел форсирует первое применение фильтров независимо от
 * переданного адреса (как в OLD_PROJECT msg_receiver_task).
 */
bsp_status_t sul_transport_can_init(void);

/**
 * @brief (Пере)настроить RX-фильтры (PACKET1..5) под адрес станции.
 *
 * НКУ-CAN кодирует адрес станции в ID: PACKET1..4 — биты [7:4] (group4 =
 * addr<<4), PACKET5 — биты [8:6] (group6 = addr<<6, протокол отводит под
 * него только 3 бита). Дёшево звать на каждой итерации приёма — реальная
 * переконфигурация Message Buffer'ов FlexCAN происходит только при
 * фактическом изменении адреса (внутренний diff, сентинел на первый вызов).
 * Эталон — OLD_PROJECT msg_receiver_task/apply_nku_can_filters().
 *
 * @param nku_address 0..15; вне диапазона — приводится к 15.
 */
bsp_status_t sul_transport_can_set_address(uint8_t nku_address);

/**
 * @brief Принять один кадр и перевести в sul_frame_t.
 *
 * @warning p_out->p_data валиден ТОЛЬКО до следующего вызова этой функции
 * (внутренний статический буфер, единственный ожидаемый вызыватель — задача
 * sul_rx с последовательным receive→decode). Не сохранять sul_frame_t между
 * итерациями цикла приёма.
 *
 * @param timeout_ms  таймаут ожидания кадра, 0 = не блокироваться
 * @param p_out       заполняется только при возврате BSP_OK
 * @return BSP_OK, BSP_ERR_TIMEOUT
 */
bsp_status_t sul_transport_can_receive(uint32_t timeout_ms, sul_frame_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_TRANSPORT_CAN_H_ */

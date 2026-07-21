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
 * @brief bsp_can_init() + RX-фильтры под активные ID НКУ-CAN.
 *
 * Фаза 1: фильтры на PACKET1(0x506)/PACKET3(0x508), адрес станции 0
 * (хардкод — совпадает с decode-стороной nku_can.c). Фаза 3 параметризует
 * оба конца из настроек одновременно.
 */
bsp_status_t sul_transport_can_init(void);

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

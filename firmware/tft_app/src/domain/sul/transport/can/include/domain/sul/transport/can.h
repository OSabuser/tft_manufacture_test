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
 * Вызывающий обязан сразу после этого позвать `set_address` СВОЕГО протокола
 * (ниже) — до этого не принимается ничего. Внутренний набор «применённых
 * фильтров» пуст, поэтому первый вызов применяется всегда, независимо от
 * переданного адреса (как сентинел в OLD_PROJECT msg_receiver_task).
 */
bsp_status_t sul_transport_can_init(void);

/* ── RX-фильтры: по одной функции на CAN-протокол ────────────────────────────
 *
 * Раскладка фильтров у протоколов РАЗНАЯ (НКУ-CAN — 7 Message Buffer'ов,
 * УИМ-6100 — один), поэтому одной «set_address» на всех быть не может.
 * Внутри обе строят свой набор и отдают общему применятелю, который:
 *   - сравнивает ВЕСЬ набор (не только адрес) → no-op, если ничего не
 *     изменилось; дёшево звать на каждой итерации приёма;
 *   - при отличии СНИМАЕТ все прежние фильтры перед установкой новых, иначе
 *     «лишние» MB прежнего протокола продолжали бы пропускать чужие кадры.
 *
 * Новый CAN-протокол = ещё одна такая функция рядом (см. ADDING_PROTOCOL.md §6).
 */

/**
 * @brief (Пере)настроить RX-фильтры под адрес станции НКУ-CAN.
 *
 * НКУ-CAN кодирует адрес станции в ID: PACKET1..4 — биты [7:4] (group4 =
 * addr<<4), PACKET5 — биты [8:6] (group6 = addr<<6, протокол отводит под
 * него только 3 бита). Плюс два wildcard-фильтра удалённой адресации
 * (`0x4X1`/`0x5XB` с любым X) — без них фича не работает вовсе.
 * Эталон — OLD_PROJECT msg_receiver_task/apply_nku_can_filters() +
 * apply_remote_addr_filters().
 *
 * @param nku_address 0..15; вне диапазона — приводится к 15.
 */
bsp_status_t sul_transport_can_set_address_nku(uint8_t nku_address);

/**
 * @brief (Пере)настроить RX-фильтр под адрес индикатора УИМ-6100.
 *
 * У УИМ CAN ID кадра РАВЕН адресу индикатора (не база+сдвиг) — нужен ровно
 * один точный фильтр. Эталон — OLD_PROJECT_TFT4_UIM can.c/set_canrx_id().
 *
 * @param uim_address адрес индикатора (1..40 этажный, 46..50 роли)
 */
bsp_status_t sul_transport_can_set_address_uim(uint8_t uim_address);

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

/**
 * @brief Отправить кадр, сформированный протоколом (sul_tx_frame_t).
 *
 * Симметрична receive(): транспорт знает КУДА и КАК, домен — только ЧТО
 * (ARCH §4). Единственный ожидаемый вызыватель — task_sul_rx.c, сразу после
 * `p_driver->take_pending_tx()`; используется, напр., обязательным откликом
 * УИМ-6100 (эталон send_response_to_station()).
 *
 * @param p_frame     кадр к отправке; len не более SUL_TX_DATA_MAX
 * @param timeout_ms  таймаут ожидания свободного TX-мейлбокса
 * @return BSP_OK, BSP_ERR_PARAM (len вне диапазона), BSP_ERR_TIMEOUT
 */
bsp_status_t sul_transport_can_send(const sul_tx_frame_t *p_frame, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_TRANSPORT_CAN_H_ */

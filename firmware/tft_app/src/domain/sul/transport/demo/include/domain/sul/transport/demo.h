/**
 * @file  demo.h
 * @brief "Транспорт" демо-протокола (ARCH §8, Фаза 3.3) — нет реальной шины.
 *
 * У демо-протокола нет транспорта в привычном смысле: sul_transport_demo_
 * receive() ничего не ждёт и не читает с шины — сразу возвращает пустой
 * sul_frame_t. Кадр — формальность ради существующего контракта sul_decode_fn
 * (см. demo.h, domain/sul/demo/) — demo_decode() его содержимое игнорирует,
 * ведёт счёт тиков сам. Существует, чтобы sul_rx_task мог опрашивать демо
 * тем же паттерном receive()->decode(), что и реальные протоколы.
 */

#ifndef DOMAIN_SUL_TRANSPORT_DEMO_H_
#define DOMAIN_SUL_TRANSPORT_DEMO_H_

#include "bsp/status.h"
#include "domain/sul.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Всегда успешно и мгновенно (не блокируется) — демо не ждёт шину.
 *
 * @param timeout_ms  игнорируется (нечего ждать)
 * @param p_out       заполняется пустым кадром (id=0, len=0)
 * @return BSP_OK всегда
 */
bsp_status_t sul_transport_demo_receive(uint32_t timeout_ms, sul_frame_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_TRANSPORT_DEMO_H_ */

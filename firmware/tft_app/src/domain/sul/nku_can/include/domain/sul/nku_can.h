/**
 * @file  nku_can.h
 * @brief Чистый декодер протокола НКУ-CAN (порт из OLD_PROJECT msg_receiver_task).
 *
 * Фаза 1 — подмножество: PACKET1 (направление) + PACKET3 (позиция кабины).
 * Остальные пакеты (PACKET2/4/5, режимы, гонг, погрузка, удалённая установка
 * адреса) — Фаза 2, см. PLAN.md.
 *
 * Адрес станции (nku_address) захардкожен в 0 — фильтры/ID без сдвига группы.
 * Фаза 3 параметризует через настройки; сигнатура decode() не изменится.
 *
 * Чистый C, без единого HAL-вызова — host-тестируется golden-векторами
 * CAN-кадров (tests/host/tft_app_sul_nku).
 */

#ifndef DOMAIN_SUL_NKU_CAN_H_
#define DOMAIN_SUL_NKU_CAN_H_

#include "domain/elevator_model.h"
#include "domain/sul.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief Состояние декодера — накопленный текущий sul_result_t.
 *
 * PACKET1 и PACKET3 несут РАЗНЫЕ поля (направление / позицию) в разных
 * кадрах — decode() обновляет только своё поле в state и отдаёт наружу
 * ПОЛНУЮ накопленную копию, а не только то, что пришло в этом кадре.
 *
 * Фаза 2 добавит сюда edge-detection состояние для гонга/погрузки/режимов
 * (аналог event_edge_t в legacy) — структура специально не голый sul_result_t,
 * чтобы не менять сигнатуру при расширении.
 */
typedef struct
{
    sul_result_t state;
} nku_can_ctx_t;

/** Сброс к состоянию по умолчанию (sul_default_state()). Вызвать перед первым decode(). */
void nku_can_init(nku_can_ctx_t *p_ctx);

/**
 * @brief decode() для реестра sul (см. sul_decode_fn в domain/sul.h).
 *
 * @param p_ctx    nku_can_ctx_t*, инициализированный nku_can_init()
 * @param p_frame  сырой CAN-кадр (id + до 8 байт data)
 * @param p_out    заполняется полной накопленной state только при SUL_STATUS_OK
 */
sul_status_t nku_can_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_NKU_CAN_H_ */

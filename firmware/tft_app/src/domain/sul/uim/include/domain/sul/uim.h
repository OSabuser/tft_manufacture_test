/**
 * @file  uim.h
 * @brief Чистый декодер протокола УИМ-6100 (порт из OLD_PROJECT_TFT4_UIM
 *        msg_receiver_task).
 *
 * Один тип кадра (в отличие от PACKET1..5 у НКУ-CAN): заголовок `0x81 0x00` +
 * четыре информационных байта W_0..W_3 (data[2..5]):
 *   W_1 (data[3]) — код сообщения (озвучка/события);
 *   W_2 (data[4]) — код этажа ЛИБО код спецрежима (один байт, два смысла —
 *                   различаются диапазоном, см. decode_floor_code());
 *   W_3 (data[5]) — направление, гонг, движение, речь, бит «нужен отклик».
 *
 * CAN ID кадра РАВЕН адресу индикатора (не база+сдвиг, как у НКУ-CAN): адрес
 * 1..40 — этажный индикатор, 46..50 — роли кабины/главного этажа. Отсюда же
 * `cop_mode` и `should_respond` — зависят от того, какую роль занимает адрес.
 *
 * Спецрежимы удерживаются ГИСТЕРЕЗИСОМ (`special_mode_cnt`) — станция чередует
 * кадры с кодом режима и кодом этажа, без удержания индикация мигала бы с
 * частотой шины; см. uim.c про attack/release.
 *
 * Чистый C, без единого HAL-вызова — host-тестируется golden-векторами
 * CAN-кадров (tests/host/tft_app_sul_uim).
 */

#ifndef DOMAIN_SUL_UIM_H_
#define DOMAIN_SUL_UIM_H_

#include "domain/elevator_model.h"
#include "domain/sul.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
 * @brief Состояние декодера — накопленный текущий sul_result_t + внутренние
 *        латчи для полей с несколькими источниками.
 *
 * Разные пакеты несут РАЗНЫЕ поля в разных кадрах — decode() обновляет только
 * пришедшее и отдаёт наружу ПОЛНУЮ накопленную копию state, а не дельту.
 *
 */
    typedef struct
    {
        sul_result_t state;

        uint8_t
            uim_address; /**< адрес индикатора 1..40/46..50 — он же CAN ID кадра (из настроек) */
        uint8_t
            actual_floor_number; /**< Актуальный номер этажа (нужен из-за того, что в байте кода этажа могут быть другие режимы) */
        bool cop_mode; /**< true, если индикатор размещён в кабине лифта (46,50 адреса) */
        bool
            should_respond; /**< true, если необходимо отправлять отклик в ответ на сообщения от СУЛ */
        uint16_t
            special_mode_cnt; /**< гистерезис удержания спецрежима (см. uim.c); 0 = обычная индикация */

    } uim_ctx_t;

    /** Сброс к состоянию по умолчанию (sul_default_state()); адрес индикатора = 46
 *  (кабина, как дефолт эталона); гистерезис спецрежимов сброшен.
 */
    void uim_init(uim_ctx_t *p_ctx);

    /**
 * @brief Задать адрес индикатора — он же CAN ID принимаемых кадров.
 *
 * Вызывать после uim_init(), значение из настроек (proto_slice[0]).
 * Значения > UIM_ADDRESS_MAX клампятся к UIM_ADDRESS_MAX.
 */
    void uim_set_address(uim_ctx_t *p_ctx, uint8_t uim_address);

    /**
 * @brief decode() для реестра sul (см. sul_decode_fn_t в domain/sul.h).
 *
 * @param p_ctx    uim_ctx_t*, инициализированный uim_init()
 * @param p_frame  сырой CAN-кадр (id + до 8 байт data)
 * @param p_out    заполняется полной накопленной state только при SUL_STATUS_OK
 */
    sul_status_t uim_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out);

    /**
 * @brief take_pending_tx() для реестра sul (см. sul_take_pending_tx_fn_t).
 *
 * Станция ТРЕБУЕТ отклик от ролей 46/47/48, когда в принятом кадре сброшен
 * бит W_3.7. Отклик — кадр `0x81 0x00` (2 байта) на CAN ID `адрес + 0x80`
 * (эталон `send_response_to_station()`).
 *
 * Чистая функция: только читает ctx после последнего uim_decode(), в шину не
 * пишет (домену она недоступна, ARCH §4). Запрос транзитен — сбрасывается в
 * начале каждого uim_decode().
 */
    bool uim_take_pending_tx(void *p_ctx, sul_tx_frame_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_UIM_H_ */

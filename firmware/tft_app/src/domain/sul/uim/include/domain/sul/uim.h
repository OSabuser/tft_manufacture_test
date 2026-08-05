/**
 * @file  nku_can.h
 * @brief Чистый декодер протокола НКУ-CAN (порт из OLD_PROJECT msg_receiver_task).
 *
 * Фаза 2 — полный разбор индикации: PACKET1 (направление, режимы, начало
 * движения), PACKET2 (перегруз), PACKET3 (позиция, гонг, временная погрузка),
 * PACKET4 (перегруз-вариант, сейсмо), PACKET5 (следующий этаж). Удалённая
 * установка адреса (0x4X1/0x5XB, Фаза 3.5, REMOTE_ADDRES_SETUP.pdf) —
 * decode() только распознаёт и выставляет запрос в ctx; саму запись в
 * settings_store делает app-слой через generic-канал sul_take_pending_
 * write_fn_t (domain/sul.h) — nku_can_take_pending_write() ниже, decode()
 * остаётся чистой функцией и не пишет настройки сам.
 *
 * Адрес станции (nku_address) захардкожен в 0 — фильтры/ID без сдвига группы.
 * Фаза 3 параметризует через настройки; сигнатура decode() не изменится.
 *
 * Чистый C, без единого HAL-вызова — host-тестируется golden-векторами
 * CAN-кадров (tests/host/tft_app_sul_nku).
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

        uint8_t uim_address; /**< адрес станции 1..50 — сдвиг ID пакетов (из настроек) */
        uint8_t
            actual_floor_number; /**< Актуальный номер этажа (нужен из-за того, что в байте кода этажа могут быть другие режимы) */
        bool cop_mode; /**< true, если индикатор размещён в кабине лифта (46,50 адреса) */
        bool
            should_respond; /**< true, если необходимо отправлять отклик в ответ на сообщения от СУЛ */

    } uim_ctx_t;

    /** Сброс к состоянию по умолчанию (sul_default_state()); адрес станции = 46;
 *  состояние удалённой адресации — «анонса/запроса не было». 
 */
    void uim_init(uim_ctx_t *p_ctx);

    /**
 * @brief Задать адрес индикатора (1..50) — сдвиг ID пакетов 
 *
 * Вызывать после uim_init(), значение из настроек (proto_slice[0]).
 * Значения > 50 клампятся к 1.
 */
    void uim_set_address(uim_ctx_t *p_ctx, uint8_t uim_address);

    /**
 * @brief decode() для реестра sul (см. sul_decode_fn в domain/sul.h).
 *
 * @param p_ctx    uim_ctx_t*, инициализированный uim_init()
 * @param p_frame  сырой CAN-кадр (id + до 8 байт data)
 * @param p_out    заполняется полной накопленной state только при SUL_STATUS_OK
 */
    sul_status_t uim_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_UIM_H_ */

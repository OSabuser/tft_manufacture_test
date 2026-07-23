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

#ifndef DOMAIN_SUL_NKU_CAN_H_
#define DOMAIN_SUL_NKU_CAN_H_

#include "domain/elevator_model.h"
#include "domain/sul.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Сентинел «анонса/запроса ещё не было» для полей удалённой адресации ниже —
 *  вне диапазона валидных X (4-битный нибл ID, 0..15). */
#define NKU_REMOTE_ADDR_NONE 0xFFU

/**
 * @brief Состояние декодера — накопленный текущий sul_result_t + внутренние
 *        латчи для полей с несколькими источниками.
 *
 * Разные пакеты несут РАЗНЫЕ поля в разных кадрах — decode() обновляет только
 * пришедшее и отдаёт наружу ПОЛНУЮ накопленную копию state, а не дельту.
 *
 * Некоторые ВЫХОДНЫЕ поля кормятся НЕСКОЛЬКИМИ пакетами и должны быть их OR,
 * иначе пакет-без-сигнала сбросил бы флаг, выставленный другим пакетом (боевой
 * баг-класс из legacy — там держались parallel-флаги overload_flag/_new,
 * lading_flag/time_lading). Держим эти под-источники раздельно в ctx и
 * пересчитываем выход как OR после каждого пакета:
 *   state.overload = overload_p2 || overload_p4;
 *   state.lading   = lading_instr || (lading_secs > 0).
 */
typedef struct
{
    sul_result_t state;

    /* Раздельные под-источники (см. докстроку выше). */
    bool    overload_p2;   /**< PACKET2: data[7] & 0x40         */
    bool    overload_p4;   /**< PACKET4: data[5] & 0x40         */
    bool    lading_instr;  /**< PACKET1: код режима 0x10 (инструментальная) */
    uint8_t current_level; /**< PACKET1: data[3] & 0x3F — числовой уровень остановки,
                                для гейта «следующего этажа» в PACKET5           */
    uint8_t nku_address;   /**< адрес станции 0..15 — сдвиг ID пакетов (из настроек) */

    /**
     * @brief Удалённая установка адреса (REMOTE_ADDRES_SETUP.pdf, §3.5).
     *
     * 1. Кадр 0x4X1 — X (биты [7:4] ID) объявляет адрес станции управления,
     *    копируется сюда безусловно (не во флеш — это делает app-слой).
     * 2. Кадр 0x5XB — команда в старшем нибле data[3]; "2" запускает запись,
     *    но только если X ЭТОГО кадра совпадает с последним объявленным
     *    (согласовано — строже буквы PDF, которая номинально допускает
     *    любой X у командного кадра).
     * decode() НЕ пишет settings — только выставляет pending_remote_write_
     * addr; nku_can_take_pending_write() ниже транслирует его в generic
     * sul_slice_write_t для app-слоя (идемпотентность — сравнение с текущим
     * сохранённым значением — общий гейт для ЛЮБОГО протокола, живёт в
     * task_sul_rx.c, не здесь).
     */
    uint8_t remote_addr_candidate;     /**< последний X из 0x4X1; NKU_REMOTE_ADDR_NONE — анонса не было */
    uint8_t pending_remote_write_addr; /**< валиден ТОЛЬКО сразу после decode() этого вызова; NKU_REMOTE_ADDR_NONE — команды в этом кадре не было */
} nku_can_ctx_t;

/** Сброс к состоянию по умолчанию (sul_default_state()); адрес станции = 0;
 *  состояние удалённой адресации — «анонса/запроса не было». */
void nku_can_init(nku_can_ctx_t *p_ctx);

/**
 * @brief Задать адрес станции (0..15) — сдвиг ID пакетов (group4/group6).
 *
 * Вызывать после nku_can_init(), значение из настроек (proto_slice[0], Фаза 3.1).
 * Значения > 15 клампятся к 15. Для адреса 0 поведение как в Фазах 1/2.
 */
void nku_can_set_address(nku_can_ctx_t *p_ctx, uint8_t nku_address);

/**
 * @brief decode() для реестра sul (см. sul_decode_fn в domain/sul.h).
 *
 * @param p_ctx    nku_can_ctx_t*, инициализированный nku_can_init()
 * @param p_frame  сырой CAN-кадр (id + до 8 байт data)
 * @param p_out    заполняется полной накопленной state только при SUL_STATUS_OK
 */
sul_status_t nku_can_decode(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out);

/**
 * @brief Реализация sul_take_pending_write_fn_t (domain/sul.h) для НКУ-CAN —
 *        удалённая установка адреса (§3.5).
 *
 * @param p_ctx  nku_can_ctx_t* после последнего decode()
 * @param p_out  slice_offset=0 (proto_slice[0] = адрес), value = запрошенный
 *               адрес; заполняется только при возврате true
 * @return true, если pending_remote_write_addr валиден в этом ctx (декодер
 *         только что распознал команду записи — см. nku_can_ctx_t выше).
 *         Идемпотентность НЕ его забота — сравнение с текущим сохранённым
 *         значением делает вызывающий (общий гейт для любого протокола).
 */
bool nku_can_take_pending_write(void *p_ctx, sul_slice_write_t *p_out);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_NKU_CAN_H_ */

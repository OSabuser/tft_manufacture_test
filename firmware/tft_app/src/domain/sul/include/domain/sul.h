/**
 * @file  sul.h
 * @brief Реестр драйверов СУЛ (ARCH.md §6) — «одна прошивка — много протоколов».
 *
 * Драйвер = чистый декодер (host-тестируемый, без HAL) + отдельный тонкий
 * транспорт-адаптер (HW, живёт в sul/transport/<bus>). Декодер НЕ владеет
 * состоянием сам — состояние (например, накопленная позиция между PACKET1 и
 * PACKET3 у НКУ-CAN) держит caller в ctx и передаёт указатель на каждый вызов
 * decode(). Это позволяет декодеру оставаться описанным одной чистой функцией
 * и не тянуть за собой выделение памяти/жизненный цикл.
 *
 * Фаза 1 — одна запись реестра (nku_can), активный драйвер хардкожен.
 * Фаза 3 добавит выбор активного протокола из настроек — сигнатура
 * `sul_registry_active()` не изменится, изменится только то, что она
 * возвращает.
 */

#ifndef DOMAIN_SUL_H_
#define DOMAIN_SUL_H_

#include "domain/elevator_model.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Результат decode() одного кадра/пакета. */
typedef enum
{
    SUL_STATUS_OK = 0, /**< кадр распознан, ctx и *p_out обновлены          */
    SUL_STATUS_IGNORED, /**< кадр не для этого драйвера — *p_out не тронут   */
    SUL_STATUS_ERR, /**< кадр совпал по ID, но малформирован (DLC и т.п.)  */
} sul_status_t;

/**
 * @brief Кадр транспортного уровня, нейтральный к шине (CAN/UART/...).
 *
 * Транспорт-адаптер (HW) заполняет её из своего протокола (для CAN — id и
 * data/len из bsp_can_frame_t); декодер (чистый C) её только читает.
 */
typedef struct
{
    uint32_t id; /**< CAN ID либо адрес/маркер кадра другого транспорта */
    uint8_t bus; /**< на будущее — несколько шин одного типа (0 = единственная) */
    const uint8_t *p_data;
    uint16_t len;
} sul_frame_t;

/**
 * @brief Чистая функция декодирования — БЕЗ единого HAL-вызова.
 *
 * @param p_ctx    состояние драйвера (владеет caller, см. докстрок файла)
 * @param p_frame  один кадр транспортного уровня
 * @param p_out    заполняется только при SUL_STATUS_OK
 */
typedef sul_status_t (*sul_decode_fn)(void *p_ctx, const sul_frame_t *p_frame, sul_result_t *p_out);

typedef struct
{
    uint8_t id; /**< стабильный идентификатор протокола */
    const char *p_name; /**< для меню (Фаза 3)            */
    sul_decode_fn decode;
} sul_driver_t;

/**
 * @brief Идентификаторы протоколов — стабильны, не переиспользовать.
 *
 * Список открыт (ARCH §2.2): УЭЛ, УКЛ, НКУ-SD7, УИМ добавляются в Фазе 8.
 */
enum
{
    SUL_PROTOCOL_NKU_CAN = 0U,
};

/**
 * @brief Активный драйвер. Фаза 1 — единственная запись, хардкод.
 * @return указатель на статический дескриптор, никогда NULL.
 */
const sul_driver_t *sul_registry_active(void);

/**
 * @brief Найти драйвер по id (для будущего выбора по настройкам, Фаза 3).
 * @return NULL, если протокол не зарегистрирован.
 */
const sul_driver_t *sul_registry_find(uint8_t id);

#ifdef __cplusplus
}
#endif

#endif /* DOMAIN_SUL_H_ */

#ifndef CAN_H_
#define CAN_H_

#include "bsp/status.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── Константы ── */

#define BSP_CAN_DATA_MAX_LEN 8U
#define BSP_CAN_FILTER_MAX   16U /* MB6..MB21 под RX-фильтры */

/* ── Типы ── */

typedef struct bsp_can_frame_s
{
    uint32_t id;      /* 11-bit STD или 29-bit EXT */
    uint8_t dlc;      /* 0..8 */
    bool is_extended; /* false = STD, true = EXT */
    bool is_remote;   /* RTR */
    uint8_t data[BSP_CAN_DATA_MAX_LEN];
} bsp_can_frame_t;

typedef struct bsp_can_config_s
{
    uint32_t bitrate;
} bsp_can_config_t;

/**
 * Callback из ISR-контекста.
 * Реализация НЕ ДОЛЖНА блокироваться.
 * Типичное использование: xQueueSendFromISR().
 */
typedef void (*bsp_can_rx_callback_t)(const bsp_can_frame_t *p_frame, void *p_user_ctx);

/* ── Init / Deinit ── */

bsp_status_t bsp_can_init(const bsp_can_config_t *p_config);
void bsp_can_deinit(void);

/* ── Фильтрация ── */

/**
 * Настроить RX-фильтр на конкретный Message Buffer.
 *
 * @param index       0 .. BSP_CAN_FILTER_MAX-1
 * @param id          CAN ID для фильтрации
 * @param mask        битовая маска (1 = проверять, 0 = игнорировать)
 * @param is_extended true = 29-bit EXT, false = 11-bit STD
 */
bsp_status_t bsp_can_set_filter(uint8_t index, uint32_t can_id, uint32_t mask, bool is_extended);

/**
 * Деактивировать ВСЕ ранее настроенные RX-фильтры.
 *
 * Симметрична bsp_can_set_filter(): без неё набор фильтров можно только
 * пополнять, но не заменить. Нужна, когда меняется РАСКЛАДКА фильтров, а не
 * только их ID — напр. при переключении протокола СУЛ (одному нужно 7 MB,
 * другому 1: без снятия «лишние» MB остались бы активными и пропускали чужие
 * кадры).
 *
 * После вызова не принимается НИЧЕГО, пока не настроен хотя бы один фильтр
 * (или bsp_can_accept_all()). Ring buffer не трогает.
 */
bsp_status_t bsp_can_clear_filters(void);

/** Принимать все фреймы (сброс всех фильтров). */
bsp_status_t bsp_can_accept_all(void);

/* ── TX ── */

/**
 * Отправить CAN-фрейм. Блокируется до завершения или таймаута.
 *
 * @param p_frame     фрейм для отправки
 * @param timeout_ms  таймаут в мс (0 = без ожидания)
 * @return BSP_OK, BSP_ERR_TIMEOUT, BSP_ERR_PARAM
 */
bsp_status_t bsp_can_send(const bsp_can_frame_t *p_frame, uint32_t timeout_ms);

/* ── RX: polling ── */

/**
 * Принять CAN-фрейм (polling). Блокируется до приёма или таймаута.
 *
 * @param p_frame     буфер для принятого фрейма
 * @param timeout_ms  таймаут в мс (0 = проверить и вернуться)
 * @return BSP_OK, BSP_ERR_TIMEOUT
 */
bsp_status_t bsp_can_receive(bsp_can_frame_t *p_frame, uint32_t timeout_ms);

/* ── RX: callback (для FreeRTOS bridge) ── */

/**
 * Зарегистрировать callback для приёма из ISR.
 * При регистрации callback, polling через bsp_can_receive() отключается.
 *
 * @param callback    функция-обработчик (NULL = отключить callback)
 * @param p_user_ctx  пользовательский контекст, передаётся в callback
 */
bsp_status_t bsp_can_register_rx_callback(bsp_can_rx_callback_t p_callback, void *p_user_ctx);

#endif /* CAN_H_ */
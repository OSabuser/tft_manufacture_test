/**
 * @file  can_mocks.h
 * @brief Объявления fff-заглушек bsp_can.
 *
 * Включать только в test-файлах, не в продакшн-коде.
 *
 * Порядок include в тест-файле:
 *   1. fff.h
 *   2. DEFINE_FFF_GLOBALS;
 *   3. bsp/can.h       ← публичный API (сигнатуры функций)
 *   4. can_mock.h      ← FAKE_* структуры
 */

#ifndef CAN_MOCKS_H
#define CAN_MOCKS_H

#include "bsp/can.h"
#include "fff.h"

/* -------------------------------------------------------------------------- */
/* Объявления заглушек (DEFINE_ живут в can_mocks.c)                            */
/* -------------------------------------------------------------------------- */

/* Init / Deinit */
DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_init, const bsp_can_config_t *);
DECLARE_FAKE_VOID_FUNC(bsp_can_deinit);

/* Фильтрация */
DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_set_filter, uint8_t, uint32_t, uint32_t, bool);
DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_accept_all);

/* TX */
DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_send, const bsp_can_frame_t *, uint32_t);

/* RX: polling */
DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_receive, bsp_can_frame_t *, uint32_t);

/* RX: callback */
DECLARE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_register_rx_callback, bsp_can_rx_callback_t, void *);

/* -------------------------------------------------------------------------- */
/* Удобный макрос: сброс всех заглушек в setUp()                               */
/* -------------------------------------------------------------------------- */

/* clang-format off */
#define CAN_MOCK_RESET_ALL()                        \
    RESET_FAKE(bsp_can_init);                       \
    RESET_FAKE(bsp_can_deinit);                     \
    RESET_FAKE(bsp_can_set_filter);                 \
    RESET_FAKE(bsp_can_accept_all);                 \
    RESET_FAKE(bsp_can_send);                       \
    RESET_FAKE(bsp_can_receive);                    \
    RESET_FAKE(bsp_can_register_rx_callback)
/* clang-format on */

#endif /* CAN_MOCKS_H */
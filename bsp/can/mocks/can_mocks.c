/**
 * @file  can_mocks.c
 * @brief fff-заглушки bsp_can для host unit-тестов (Humble Object).
 *
 * Подключается вместо bsp_can.c при BUILD_TESTS_HOST.
 * Не требует SDK, FlexCAN, ring buffer.
 *
 * Использование в тесте:
 *
 *   #include "fff.h"
 *   DEFINE_FFF_GLOBALS;
 *
 *   #include "bsp/can.h"
 *   #include "can_mocks.h"
 *
 *   void setUp(void)    { CAN_MOCK_RESET_ALL(); }
 *   void tearDown(void) {}
 *
 *   void test_something(void) {
 *       bsp_can_send_fake.return_val = BSP_OK;
 *       // ... вызываем код под тестом ...
 *       TEST_ASSERT_EQUAL(1, bsp_can_send_fake.call_count);
 *   }
 */

#include "can_mocks.h"

/* -------------------------------------------------------------------------- */
/* Определения fff-заглушек                                                    */
/* -------------------------------------------------------------------------- */

/* Init / Deinit */
DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_init, const bsp_can_config_t *);
DEFINE_FAKE_VOID_FUNC(bsp_can_deinit);

/* Фильтрация */
DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_set_filter, uint8_t, uint32_t, uint32_t, bool);
DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_accept_all);

/* TX */
DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_send, const bsp_can_frame_t *, uint32_t);

/* RX: polling */
DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_receive, bsp_can_frame_t *, uint32_t);

/* RX: callback */
DEFINE_FAKE_VALUE_FUNC(bsp_status_t, bsp_can_register_rx_callback, bsp_can_rx_callback_t, void *);
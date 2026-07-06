/**
 * @file  test_bsp_can.c
 * @brief Host unit-тесты для bsp_can (категория B — SDK через fff).
 *
 * Тестируемый модуль: bsp/can/src/bsp_can.c
 * Зависимости:        utils/ring_buffer/ring_buffer.c (реальный, категория A)
 *
 * Порядок include:
 *   1. unity.h + fff.h + DEFINE_FFF_GLOBALS
 *   2. stub-хедеры с типами (fsl_flexcan.h, clock_config.h)
 *   3. FAKE_* объявления для SDK-функций
 *   4. bsp/can.h — тестируемый модуль (последним)
 */

#include "fff.h"
#include "unity.h"

DEFINE_FFF_GLOBALS;

#include "clock_config.h"
#include "fsl_clock.h"
#include "fsl_flexcan.h"
/* ══════════════════════════════════════════════════════════════════════
 *  fff-фейки SDK-функций
 * ══════════════════════════════════════════════════════════════════════ */

/* Init / Config */
FAKE_VOID_FUNC(FLEXCAN_GetDefaultConfig, flexcan_config_t *);
FAKE_VALUE_FUNC(bool, FLEXCAN_CalculateImprovedTimingValues, CAN_Type *, uint32_t, uint32_t,
                flexcan_timing_config_t *);
FAKE_VOID_FUNC(FLEXCAN_Init, CAN_Type *, const flexcan_config_t *, uint32_t);
FAKE_VOID_FUNC(FLEXCAN_Deinit, CAN_Type *);

/* MB config */
FAKE_VOID_FUNC(FLEXCAN_SetTxMbConfig, CAN_Type *, uint8_t, bool);
FAKE_VOID_FUNC(FLEXCAN_SetRxMbConfig, CAN_Type *, uint8_t, const flexcan_rx_mb_config_t *, bool);
FAKE_VOID_FUNC(FLEXCAN_SetRxIndividualMask, CAN_Type *, uint8_t, uint32_t);

/* TX / RX */
FAKE_VALUE_FUNC(status_t, FLEXCAN_WriteTxMb, CAN_Type *, uint8_t, const flexcan_frame_t *);
FAKE_VALUE_FUNC(status_t, FLEXCAN_ReadRxMb, CAN_Type *, uint8_t, flexcan_frame_t *);

/* Status flags */
FAKE_VALUE_FUNC(uint64_t, FLEXCAN_GetMbStatusFlags, CAN_Type *, uint64_t);
FAKE_VOID_FUNC(FLEXCAN_ClearMbStatusFlags, CAN_Type *, uint64_t);

/* bsp_tick — управляемый «таймер» для тестов таймаутов */
FAKE_VALUE_FUNC(uint32_t, bsp_tick_get_ms);

/* ERRATA 50235 workaround (см. bsp_can_init()) */
FAKE_VOID_FUNC(CLOCK_EnableClock, clock_ip_name_t);

/* ══════════════════════════════════════════════════════════════════════
 *  Тестируемый модуль (после всех фейков!)
 * ══════════════════════════════════════════════════════════════════════ */

#include "bsp/can.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Helpers
 * ══════════════════════════════════════════════════════════════════════ */

/** Дефолтная конфигурация для тестов. */
static const bsp_can_config_t s_default_cfg = { .bitrate = 500000U };

/**
 * Инициализировать модуль с дефолтными параметрами.
 * Вызывает bsp_can_init() с timing_calc = true.
 */
static bsp_status_t helper_init_default(void)
{
    FLEXCAN_CalculateImprovedTimingValues_fake.return_val = true;
    return bsp_can_init(&s_default_cfg);
}

/**
 * custom_fake для ReadRxMb — заполняет фрейм заданными данными.
 * Используется через FLEXCAN_ReadRxMb_fake.custom_fake.
 */
static flexcan_frame_t s_injected_rx_frame;

static status_t read_rx_mb_inject(CAN_Type *p_base, uint8_t mb_idx, flexcan_frame_t *p_frame)
{
    (void) p_base;
    (void) mb_idx;
    *p_frame = s_injected_rx_frame;
    return kStatus_Success;
}

/**
 * custom_fake для GetMbStatusFlags — возвращает флаг для первого RX MB
 * только при первом вызове, потом 0.
 */
static uint32_t s_mb_flags_call_count;

static uint64_t get_mb_flags_once(CAN_Type *p_base, uint64_t mask)
{
    (void) p_base;
    s_mb_flags_call_count++;
    /* Первый вызов — флаг есть, остальные — нет */
    if (s_mb_flags_call_count == 1U)
    {
        return mask;
    }
    return 0U;
}

/**
 * custom_fake для bsp_tick_get_ms — линейно нарастающее время.
 */
static uint32_t s_tick_ms;

static uint32_t tick_advancing(void)
{
    uint32_t val = s_tick_ms;
    s_tick_ms += 1U;
    return val;
}

/* ══════════════════════════════════════════════════════════════════════
 *  setUp / tearDown
 * ══════════════════════════════════════════════════════════════════════ */

void setUp(void)
{
    RESET_FAKE(FLEXCAN_GetDefaultConfig);
    RESET_FAKE(FLEXCAN_CalculateImprovedTimingValues);
    RESET_FAKE(FLEXCAN_Init);
    RESET_FAKE(FLEXCAN_Deinit);
    RESET_FAKE(FLEXCAN_SetTxMbConfig);
    RESET_FAKE(FLEXCAN_SetRxMbConfig);
    RESET_FAKE(FLEXCAN_SetRxIndividualMask);
    RESET_FAKE(FLEXCAN_WriteTxMb);
    RESET_FAKE(FLEXCAN_ReadRxMb);
    RESET_FAKE(FLEXCAN_GetMbStatusFlags);
    RESET_FAKE(FLEXCAN_ClearMbStatusFlags);
    RESET_FAKE(bsp_tick_get_ms);
    RESET_FAKE(CLOCK_EnableClock);
    FFF_RESET_HISTORY();

    s_mb_flags_call_count = 0U;
    s_tick_ms             = 0U;
    (void) memset(&s_injected_rx_frame, 0, sizeof(s_injected_rx_frame));
    (void) memset(&g_stub_can2, 0, sizeof(g_stub_can2));

    /* Деинициализировать модуль между тестами (сброс static-состояния). */
    bsp_can_deinit();
    RESET_FAKE(FLEXCAN_Deinit);
}

void tearDown(void)
{
}

/* ══════════════════════════════════════════════════════════════════════
 *  Init / Deinit
 * ══════════════════════════════════════════════════════════════════════ */

void test_init_success(void)
{
    bsp_status_t s = helper_init_default();

    TEST_ASSERT_EQUAL(BSP_OK, s);
    TEST_ASSERT_EQUAL(1, FLEXCAN_GetDefaultConfig_fake.call_count);
    TEST_ASSERT_EQUAL(1, FLEXCAN_CalculateImprovedTimingValues_fake.call_count);
    TEST_ASSERT_EQUAL(1, FLEXCAN_Init_fake.call_count);
    /* MB0 = reserved (ERR005829), MB1 = TX */
    TEST_ASSERT_EQUAL(1, FLEXCAN_SetTxMbConfig_fake.call_count);
    TEST_ASSERT_EQUAL(1, CLOCK_EnableClock_fake.call_count);
    TEST_ASSERT_EQUAL(kCLOCK_Lpuart1, CLOCK_EnableClock_fake.arg0_val);
}

void test_init_null_config(void)
{
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_init(NULL));
    TEST_ASSERT_EQUAL(0, FLEXCAN_Init_fake.call_count);
}

void test_init_zero_bitrate(void)
{
    bsp_can_config_t cfg = { .bitrate = 0U };
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_init(&cfg));
}

void test_init_bitrate_too_high(void)
{
    bsp_can_config_t cfg = { .bitrate = 2000000U };
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_init(&cfg));
}

void test_init_timing_calc_fails(void)
{
    FLEXCAN_CalculateImprovedTimingValues_fake.return_val = false;

    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_init(&s_default_cfg));
    TEST_ASSERT_EQUAL(0, FLEXCAN_Init_fake.call_count);
}

void test_init_reinit_calls_deinit(void)
{
    helper_init_default();
    /* Повторная инициализация — должен вызвать Deinit. */
    helper_init_default();

    TEST_ASSERT_EQUAL(1, FLEXCAN_Deinit_fake.call_count);
    TEST_ASSERT_EQUAL(2, FLEXCAN_Init_fake.call_count);
}

void test_deinit_calls_sdk(void)
{
    helper_init_default();
    bsp_can_deinit();

    TEST_ASSERT_EQUAL(1, FLEXCAN_Deinit_fake.call_count);
}

void test_deinit_without_init_is_noop(void)
{
    bsp_can_deinit();
    TEST_ASSERT_EQUAL(0, FLEXCAN_Deinit_fake.call_count);
}

/* ══════════════════════════════════════════════════════════════════════
 *  TX — bsp_can_send()
 * ══════════════════════════════════════════════════════════════════════ */

void test_send_success(void)
{
    helper_init_default();

    FLEXCAN_WriteTxMb_fake.return_val = kStatus_Success;
    /* Флаг TX complete — сразу готов. */
    FLEXCAN_GetMbStatusFlags_fake.return_val = (uint64_t) 1U << 1U;
    bsp_tick_get_ms_fake.return_val          = 0U;

    bsp_can_frame_t frame = {
        .id          = 0x123U,
        .dlc         = 8U,
        .is_extended = false,
        .is_remote   = false,
        .data        = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 },
    };

    TEST_ASSERT_EQUAL(BSP_OK, bsp_can_send(&frame, 100U));
    TEST_ASSERT_EQUAL(1, FLEXCAN_WriteTxMb_fake.call_count);
    TEST_ASSERT_EQUAL(1, FLEXCAN_ClearMbStatusFlags_fake.call_count);
}

void test_send_null_frame(void)
{
    helper_init_default();
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_send(NULL, 100U));
}

void test_send_dlc_over_8(void)
{
    helper_init_default();
    bsp_can_frame_t frame = { .id = 0x123U, .dlc = 9U };
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_send(&frame, 100U));
}

void test_send_without_init(void)
{
    bsp_can_frame_t frame = { .id = 0x123U, .dlc = 1U };
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_send(&frame, 100U));
}

void test_send_mb_busy(void)
{
    helper_init_default();
    FLEXCAN_WriteTxMb_fake.return_val = kStatus_Fail;

    bsp_can_frame_t frame = { .id = 0x123U, .dlc = 1U };
    TEST_ASSERT_EQUAL(BSP_ERR_BUSY, bsp_can_send(&frame, 100U));
}

void test_send_timeout(void)
{
    helper_init_default();

    FLEXCAN_WriteTxMb_fake.return_val        = kStatus_Success;
    FLEXCAN_GetMbStatusFlags_fake.return_val = 0U; /* Никогда не готов. */
    bsp_tick_get_ms_fake.custom_fake         = tick_advancing;

    bsp_can_frame_t frame = { .id = 0x123U, .dlc = 1U };
    TEST_ASSERT_EQUAL(BSP_ERR_TIMEOUT, bsp_can_send(&frame, 5U));
}

/* ══════════════════════════════════════════════════════════════════════
 *  Фильтрация — bsp_can_set_filter() / bsp_can_accept_all()
 * ══════════════════════════════════════════════════════════════════════ */

void test_set_filter_std(void)
{
    helper_init_default();

    bsp_status_t s = bsp_can_set_filter(0U, 0x200U, 0x7FFU, false);

    TEST_ASSERT_EQUAL(BSP_OK, s);
    TEST_ASSERT_GREATER_THAN(0, FLEXCAN_SetRxMbConfig_fake.call_count);
    TEST_ASSERT_GREATER_THAN(0, FLEXCAN_SetRxIndividualMask_fake.call_count);
}

void test_set_filter_ext(void)
{
    helper_init_default();

    bsp_status_t s = bsp_can_set_filter(0U, 0x1ABCDEF0U, 0x1FFFFFFFU, true);

    TEST_ASSERT_EQUAL(BSP_OK, s);
}

void test_set_filter_index_out_of_range(void)
{
    helper_init_default();

    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_set_filter(BSP_CAN_FILTER_MAX, 0x123U, 0x7FFU, false));
}

void test_set_filter_without_init(void)
{
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_set_filter(0U, 0x123U, 0x7FFU, false));
}

void test_accept_all(void)
{
    helper_init_default();

    bsp_status_t s = bsp_can_accept_all();

    TEST_ASSERT_EQUAL(BSP_OK, s);
    /* Должен настроить минимум 2 RX MB (STD + EXT). */
    TEST_ASSERT_GREATER_OR_EQUAL(2, FLEXCAN_SetRxMbConfig_fake.call_count);
}

void test_accept_all_clears_previous_filters(void)
{
    helper_init_default();

    /* Настроить 3 фильтра. */
    bsp_can_set_filter(0U, 0x100U, 0x7FFU, false);
    bsp_can_set_filter(1U, 0x200U, 0x7FFU, false);
    bsp_can_set_filter(2U, 0x300U, 0x7FFU, false);

    uint32_t calls_before = FLEXCAN_SetRxMbConfig_fake.call_count;

    bsp_can_accept_all();

    /* accept_all должен деактивировать предыдущие + настроить 2 новых. */
    uint32_t calls_after = FLEXCAN_SetRxMbConfig_fake.call_count;
    TEST_ASSERT_GREATER_THAN(calls_before, calls_after);
}

void test_accept_all_without_init(void)
{
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_accept_all());
}

/* ══════════════════════════════════════════════════════════════════════
 *  RX — bsp_can_receive()
 * ══════════════════════════════════════════════════════════════════════ */

void test_receive_success(void)
{
    helper_init_default();
    bsp_can_accept_all();

    /* Подготовить фрейм в SDK-формате. */
    (void) memset(&s_injected_rx_frame, 0, sizeof(s_injected_rx_frame));
    s_injected_rx_frame.id        = FLEXCAN_ID_STD(0x456U);
    s_injected_rx_frame.format    = (uint8_t) kFLEXCAN_FrameFormatStandard;
    s_injected_rx_frame.type      = (uint8_t) kFLEXCAN_FrameTypeData;
    s_injected_rx_frame.length    = 4U;
    s_injected_rx_frame.dataByte0 = 0xAAU;
    s_injected_rx_frame.dataByte1 = 0xBBU;
    s_injected_rx_frame.dataByte2 = 0xCCU;
    s_injected_rx_frame.dataByte3 = 0xDDU;

    FLEXCAN_ReadRxMb_fake.custom_fake         = read_rx_mb_inject;
    FLEXCAN_GetMbStatusFlags_fake.custom_fake = get_mb_flags_once;
    s_mb_flags_call_count                     = 0U;
    bsp_tick_get_ms_fake.return_val           = 0U;

    bsp_can_frame_t rx;
    bsp_status_t s = bsp_can_receive(&rx, 100U);

    TEST_ASSERT_EQUAL(BSP_OK, s);
    TEST_ASSERT_EQUAL(0x456U, rx.id);
    TEST_ASSERT_FALSE(rx.is_extended);
    TEST_ASSERT_EQUAL(4U, rx.dlc);
    TEST_ASSERT_EQUAL(0xAAU, rx.data[0]);
    TEST_ASSERT_EQUAL(0xBBU, rx.data[1]);
    TEST_ASSERT_EQUAL(0xCCU, rx.data[2]);
    TEST_ASSERT_EQUAL(0xDDU, rx.data[3]);
}

void test_receive_ext_frame(void)
{
    helper_init_default();
    bsp_can_accept_all();

    (void) memset(&s_injected_rx_frame, 0, sizeof(s_injected_rx_frame));
    s_injected_rx_frame.id     = FLEXCAN_ID_EXT(0x1ABCDEF0U);
    s_injected_rx_frame.format = (uint8_t) kFLEXCAN_FrameFormatExtend;
    s_injected_rx_frame.type   = (uint8_t) kFLEXCAN_FrameTypeData;
    s_injected_rx_frame.length = 2U;

    FLEXCAN_ReadRxMb_fake.custom_fake         = read_rx_mb_inject;
    FLEXCAN_GetMbStatusFlags_fake.custom_fake = get_mb_flags_once;
    s_mb_flags_call_count                     = 0U;
    bsp_tick_get_ms_fake.return_val           = 0U;

    bsp_can_frame_t rx;
    bsp_status_t s = bsp_can_receive(&rx, 100U);

    TEST_ASSERT_EQUAL(BSP_OK, s);
    TEST_ASSERT_EQUAL(0x1ABCDEF0U, rx.id);
    TEST_ASSERT_TRUE(rx.is_extended);
}

void test_receive_timeout(void)
{
    helper_init_default();
    bsp_can_accept_all();

    /* Ни один MB не готов. */
    FLEXCAN_GetMbStatusFlags_fake.return_val = 0U;
    bsp_tick_get_ms_fake.custom_fake         = tick_advancing;

    bsp_can_frame_t rx;
    TEST_ASSERT_EQUAL(BSP_ERR_TIMEOUT, bsp_can_receive(&rx, 5U));
}

void test_receive_null_frame(void)
{
    helper_init_default();
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_receive(NULL, 100U));
}

void test_receive_without_init(void)
{
    bsp_can_frame_t rx;
    TEST_ASSERT_EQUAL(BSP_ERR_PARAM, bsp_can_receive(&rx, 100U));
}

void test_receive_nonblocking_empty(void)
{
    helper_init_default();
    bsp_can_accept_all();

    FLEXCAN_GetMbStatusFlags_fake.return_val = 0U;
    bsp_tick_get_ms_fake.return_val          = 0U;

    bsp_can_frame_t rx;
    /* timeout_ms = 0 → неблокирующий опрос. */
    TEST_ASSERT_EQUAL(BSP_ERR_TIMEOUT, bsp_can_receive(&rx, 0U));
}

/* ══════════════════════════════════════════════════════════════════════
 *  Callback (заглушка)
 * ══════════════════════════════════════════════════════════════════════ */

void test_register_callback_not_supported(void)
{
    helper_init_default();

    TEST_ASSERT_EQUAL(BSP_ERR_NOT_SUPPORTED, bsp_can_register_rx_callback(NULL, NULL));
}

/* ══════════════════════════════════════════════════════════════════════
 *  Frame conversion — проверка корректности STD/EXT ID encoding
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Захват фрейма, переданного в WriteTxMb, для проверки конвертации.
 */
static flexcan_frame_t s_captured_tx_frame;

static status_t capture_tx_frame(CAN_Type *p_base, uint8_t mb_idx, const flexcan_frame_t *p_frame)
{
    (void) p_base;
    (void) mb_idx;
    s_captured_tx_frame = *p_frame;
    return kStatus_Success;
}

void test_send_std_id_encoding(void)
{
    helper_init_default();

    FLEXCAN_WriteTxMb_fake.custom_fake       = capture_tx_frame;
    FLEXCAN_GetMbStatusFlags_fake.return_val = (uint64_t) 1U << 1U;
    bsp_tick_get_ms_fake.return_val          = 0U;

    bsp_can_frame_t frame = {
        .id          = 0x7FFU,
        .dlc         = 0U,
        .is_extended = false,
    };
    bsp_can_send(&frame, 100U);

    TEST_ASSERT_EQUAL(FLEXCAN_ID_STD(0x7FFU), s_captured_tx_frame.id);
    TEST_ASSERT_EQUAL((uint8_t) kFLEXCAN_FrameFormatStandard, s_captured_tx_frame.format);
}

void test_send_ext_id_encoding(void)
{
    helper_init_default();

    FLEXCAN_WriteTxMb_fake.custom_fake       = capture_tx_frame;
    FLEXCAN_GetMbStatusFlags_fake.return_val = (uint64_t) 1U << 1U;
    bsp_tick_get_ms_fake.return_val          = 0U;

    bsp_can_frame_t frame = {
        .id          = 0x1FFFFFFFU,
        .dlc         = 0U,
        .is_extended = true,
    };
    bsp_can_send(&frame, 100U);

    TEST_ASSERT_EQUAL(FLEXCAN_ID_EXT(0x1FFFFFFFU), s_captured_tx_frame.id);
    TEST_ASSERT_EQUAL((uint8_t) kFLEXCAN_FrameFormatExtend, s_captured_tx_frame.format);
}

void test_send_data_byte_order(void)
{
    helper_init_default();

    FLEXCAN_WriteTxMb_fake.custom_fake       = capture_tx_frame;
    FLEXCAN_GetMbStatusFlags_fake.return_val = (uint64_t) 1U << 1U;
    bsp_tick_get_ms_fake.return_val          = 0U;

    bsp_can_frame_t frame = {
        .id   = 0x100U,
        .dlc  = 8U,
        .data = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 },
    };
    bsp_can_send(&frame, 100U);

    TEST_ASSERT_EQUAL_HEX8(0x11, s_captured_tx_frame.dataByte0);
    TEST_ASSERT_EQUAL_HEX8(0x22, s_captured_tx_frame.dataByte1);
    TEST_ASSERT_EQUAL_HEX8(0x33, s_captured_tx_frame.dataByte2);
    TEST_ASSERT_EQUAL_HEX8(0x44, s_captured_tx_frame.dataByte3);
    TEST_ASSERT_EQUAL_HEX8(0x55, s_captured_tx_frame.dataByte4);
    TEST_ASSERT_EQUAL_HEX8(0x66, s_captured_tx_frame.dataByte5);
    TEST_ASSERT_EQUAL_HEX8(0x77, s_captured_tx_frame.dataByte6);
    TEST_ASSERT_EQUAL_HEX8(0x88, s_captured_tx_frame.dataByte7);
}

/* ══════════════════════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════════════════════ */

int main(void)
{
    UNITY_BEGIN();

    /* Init / Deinit */
    RUN_TEST(test_init_success);
    RUN_TEST(test_init_null_config);
    RUN_TEST(test_init_zero_bitrate);
    RUN_TEST(test_init_bitrate_too_high);
    RUN_TEST(test_init_timing_calc_fails);
    RUN_TEST(test_init_reinit_calls_deinit);
    RUN_TEST(test_deinit_calls_sdk);
    RUN_TEST(test_deinit_without_init_is_noop);

    /* TX */
    RUN_TEST(test_send_success);
    RUN_TEST(test_send_null_frame);
    RUN_TEST(test_send_dlc_over_8);
    RUN_TEST(test_send_without_init);
    RUN_TEST(test_send_mb_busy);
    RUN_TEST(test_send_timeout);

    /* Фильтрация */
    RUN_TEST(test_set_filter_std);
    RUN_TEST(test_set_filter_ext);
    RUN_TEST(test_set_filter_index_out_of_range);
    RUN_TEST(test_set_filter_without_init);
    RUN_TEST(test_accept_all);
    RUN_TEST(test_accept_all_clears_previous_filters);
    RUN_TEST(test_accept_all_without_init);

    /* RX */
    RUN_TEST(test_receive_success);
    RUN_TEST(test_receive_ext_frame);
    RUN_TEST(test_receive_timeout);
    RUN_TEST(test_receive_null_frame);
    RUN_TEST(test_receive_without_init);
    RUN_TEST(test_receive_nonblocking_empty);

    /* Callback */
    RUN_TEST(test_register_callback_not_supported);

    /* Frame conversion */
    RUN_TEST(test_send_std_id_encoding);
    RUN_TEST(test_send_ext_id_encoding);
    RUN_TEST(test_send_data_byte_order);

    return UNITY_END();
}
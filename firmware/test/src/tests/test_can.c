/*
 * test_can — HIL-тест CAN-интерфейса (FlexCAN2, трансивер SN65HVD230D)
 *
 * 2 шага, оба направления независимо:
 *
 * Шаг 1 — RX (M5 → таргет):
 *   confirm_request("can_rx_ready")
 *   TUI командует M5: can_send(id=0x100, data=[0xDE,0xAD,0xBE,0xEF])
 *   TUI отправляет confirm(true)
 *   таргет: bsp_can_receive(&frame, 500 мс)
 *   верификация: frame.id == 0x100, frame.data == [0xDE,0xAD,0xBE,0xEF]
 *
 * Шаг 2 — TX (таргет → M5):
 *   таргет: bsp_can_send(id=0x200, data=[0xCA,0xFE,0xBA,0xBE], timeout=100 мс)
 *   confirm_request("can_tx_verify")
 *   TUI: M5.can_recv(timeout=500 мс) → проверяет id+data
 *   TUI: confirm(true) если корректно, confirm(false) если нет
 *
 * disableSelfReception=true в bsp_can — таргет не слышит свой TX,
 * верификацию TX делает только M5.
 */

#include "bsp/can.h"
#include "bsp/tick.h"
#include "test_module.h"
#include "test_runner.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
/* ── Константы ───────────────────────────────────────────────────────────── */

/** Битрейт CAN, совпадает с конфигурацией агента M5. */
#define CAN_BITRATE 125000U

/** Таймаут TX в bsp_can_send(), мс. */
#define CAN_TX_TIMEOUT_MS 100U

/**
 * Таймаут bsp_can_receive() после confirm от M5, мс.
 * M5 уже отправил фрейм до confirmed:true — 500 мс с большим запасом.
 */
#define CAN_RX_TIMEOUT_MS 500U

/** ID фрейма M5 → таргет (шаг 1). */
#define CAN_RX_EXPECTED_ID 0x100U

/** ID фрейма таргет → M5 (шаг 2). */
#define CAN_TX_ID 0x200U

/** Данные фрейма M5 → таргет (шаг 1). */
static const uint8_t K_RX_EXPECTED_DATA[] = { 0xDEU, 0xADU, 0xBEU, 0xEFU };

/** Данные фрейма таргет → M5 (шаг 2). */
static const uint8_t K_TX_DATA[] = { 0xCAU, 0xFEU, 0xBAU, 0xBEU };

#define CAN_FRAME_DLC 4U

/* ── Реализация тест-модуля ──────────────────────────────────────────────── */

/**
 * @brief Инициализация: 125 kbit/s, принимать все фреймы.
 */
static void can_init(void)
{
    const bsp_can_config_t K_CFG = { .bitrate = CAN_BITRATE };
    (void) bsp_can_init(&K_CFG);
    (void) bsp_can_accept_all();
}

/**
 * @brief Выполнение теста: шаг 1 (RX) + шаг 2 (TX).
 *
 * @return test_result_t
 */
static test_result_t can_run(void)
{
    test_result_t result = {
        .status      = TEST_STATUS_PASS,
        .duration_ms = 0U,
        .detail      = "",
    };

    /* ── Шаг 1: RX (M5 → таргет) ─────────────────────────────────────── */

    /*
     * Запросить у TUI: пусть M5 отправит тестовый фрейм.
     * M5 отправляет фрейм ДО того как подтвердить confirm(true).
     * После confirm таргет вызывает bsp_can_receive().
     */
    const confirm_params_t K_RX_PARAMS = {
        .id         = "can_rx_ready",
        .prompt     = "M5: can_send id=0x100 data=[DE AD BE EF]",
        .timeout_ms = 0U,
    };

    bool confirmed = test_runner_wait_confirm(&K_RX_PARAMS);

    if (!confirmed)
    {
        result.status = TEST_STATUS_SKIP;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE, "confirm timeout on can_rx_ready");
        return result;
    }

    /* Принять фрейм — M5 уже отправил его до confirmed:true */
    bsp_can_frame_t rx_frame;
    bsp_status_t rx_status = bsp_can_receive(&rx_frame, CAN_RX_TIMEOUT_MS);

    if (rx_status != BSP_OK)
    {
        result.status = TEST_STATUS_FAIL;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE, "can_rx_ready: no frame received");
        return result;
    }

    /* Верификация ID */
    if (rx_frame.id != CAN_RX_EXPECTED_ID)
    {
        result.status = TEST_STATUS_FAIL;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE,
                        "rx id mismatch: expected 0x%03lX got 0x%03lX",
                        (unsigned long) CAN_RX_EXPECTED_ID, (unsigned long) rx_frame.id);
        return result;
    }

    /* Верификация DLC */
    if (rx_frame.dlc != CAN_FRAME_DLC)
    {
        result.status = TEST_STATUS_FAIL;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE, "rx dlc mismatch: expected %u got %u",
                        (unsigned) CAN_FRAME_DLC, (unsigned) rx_frame.dlc);
        return result;
    }

    /* Верификация данных */
    if (memcmp(rx_frame.data, K_RX_EXPECTED_DATA, CAN_FRAME_DLC) != 0)
    {
        result.status = TEST_STATUS_FAIL;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE,
                        "rx data mismatch: got %02X %02X %02X %02X", (unsigned) rx_frame.data[0],
                        (unsigned) rx_frame.data[1], (unsigned) rx_frame.data[2],
                        (unsigned) rx_frame.data[3]);
        return result;
    }

    /* ── Шаг 2: TX (таргет → M5) ─────────────────────────────────────── */

    /*
     * Сначала отправляем фрейм — до confirm_request.
     * TUI увидит confirm_request, скажет M5 принять фрейм,
     * проверит id+data и пришлёт confirm(true/false).
     */
    bsp_can_frame_t tx_frame;
    (void) memset(&tx_frame, 0, sizeof(tx_frame));
    tx_frame.id          = CAN_TX_ID;
    tx_frame.dlc         = CAN_FRAME_DLC;
    tx_frame.is_extended = false;
    tx_frame.is_remote   = false;
    (void) memcpy(tx_frame.data, K_TX_DATA, CAN_FRAME_DLC);

    bsp_status_t tx_status = bsp_can_send(&tx_frame, CAN_TX_TIMEOUT_MS);

    if (tx_status != BSP_OK)
    {
        result.status = TEST_STATUS_FAIL;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE, "tx failed: bsp_can_send returned %d",
                        (int) tx_status);
        return result;
    }

    /*
     * Запросить верификацию у TUI: M5 должен был принять наш фрейм.
     * TUI проверяет id+data и шлёт confirmed(true) или confirmed(false).
     */
    const confirm_params_t K_TX_PARAMS = {
        .id         = "can_tx_verify",
        .prompt     = "M5: verify can_recv id=0x200 data=[CA FE BA BE]",
        .timeout_ms = 0U,
    };

    confirmed = test_runner_wait_confirm(&K_TX_PARAMS);

    if (!confirmed)
    {
        result.status = TEST_STATUS_FAIL;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE,
                        "can_tx_verify: M5 did not confirm tx frame");
        return result;
    }

    return result;
}

/* ── Дескриптор тест-модуля ──────────────────────────────────────────────── */

/** @brief Дескриптор для регистрации в test_runner. */
const test_module_t K_TEST_CAN = {
    .id                 = "can",
    .name               = "CAN Network",
    .critical           = false,
    .requires_hil       = true,
    .pre_confirm_prompt = NULL,
    .init               = can_init,
    .run                = can_run,
    .deinit             = NULL,
};
/**
 * @file  bsp_can.c
 * @brief BSP CAN — единый модуль для bare-metal и FreeRTOS.
 *
 * Использует FlexCAN2 (CAN2 base) на MIMXRT1052.
 * Пины: GPIO_AD_B0_14 (FLEXCAN2_TX), GPIO_AD_B0_15 (FLEXCAN2_RX).
 * Трансивер: SN65HVD230D.
 *
 * Распределение Message Buffers:
 *   MB 0       — Зарезервирован (ERR005829: inactive TX для workaround)
 *   MB 1       — TX (отправка)
 *   MB 2..17   — RX с индивидуальными фильтрами (BSP_CAN_FILTER_MAX = 16)
 *
 * Режим приёма:
 *   - Polling (по умолчанию): bsp_can_receive() опрашивает все RX MB
 *     и складывает найденные фреймы во внутренний ring buffer.
 *   - Callback (заглушка): bsp_can_register_rx_callback() возвращает
 *     BSP_ERR_NOT_SUPPORTED в первой итерации.
 */

#include "bsp/can.h"

#include "bsp/tick.h"
#include "clock_config.h"
#include "fsl_clock.h"
#include "fsl_flexcan.h"
#include "ring_buffer/ring_buffer.h"

#include <stdbool.h>
#include <string.h>

/* ══════════════════════════════════════════════════════════════════════
 *  Приватные константы
 * ══════════════════════════════════════════════════════════════════════ */

/** Периферийный блок FlexCAN2 (на RT1052 CAN1-пины → CAN2 base). */
#define BSP_CAN_BASE CAN2

/** Источник тактирования FlexCAN — смотри Config Tools. */
#define BSP_CAN_CLK_SRC     kFLEXCAN_ClkSrcPeri
#define BSP_CAN_CLK_FREQ_HZ BOARD_BOOTCLOCKRUN_CAN_CLK_ROOT

/* ERR005829: MB0 зарезервирован как inactive TX. */
#define RESERVED_MB_IDX 0U
#define TX_MB_IDX       1U
#define RX_MB_FIRST     2U
#define RX_MB_LAST      (RX_MB_FIRST + BSP_CAN_FILTER_MAX - 1U) /* 17 */
#define MAX_MB_NUM      (RX_MB_LAST + 1U)                       /* 18 */

/** Максимальная допустимая скорость CAN 2.0 */
#define BSP_CAN_BITRATE_MAX 1000000U
/**
 * Размер внутреннего ring buffer для polling-приёма (в байтах).
 * Один фрейм сериализуется в sizeof(bsp_can_frame_t) байт.
 * 256 байт ≈ 16 фреймов по 16 байт — достаточно для типичного polling.
 */
#define RX_RING_SIZE 256U

/* ══════════════════════════════════════════════════════════════════════
 *  Приватное состояние модуля
 * ══════════════════════════════════════════════════════════════════════ */

static bool g_s_initialized = false;

/** Битовая маска активных RX MB (бит i → MB (RX_MB_FIRST + i) настроен). */
static uint32_t g_s_rx_mb_active_mask;

/* Ring buffer для polling-приёма */
static uint8_t g_s_rx_ring_storage[RX_RING_SIZE];
static ring_buffer_desc_t g_s_rx_ring;

/* ══════════════════════════════════════════════════════════════════════
 *  Вспомогательные функции
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Конвертировать bsp_can_frame_t → flexcan_frame_t для TX.
 *
 * Ключевой момент: SDK хранит ID уже сдвинутым через FLEXCAN_ID_STD/EXT,
 * а data — в big-endian word format (byte0 = MSB of dataWord0).
 */

// NOLINT(cppcoreguidelines-avoid-magic-numbers)
static void frame_to_sdk(const bsp_can_frame_t *p_bsp, flexcan_frame_t *p_sdk)
{
    (void) memset(p_sdk, 0, sizeof(*p_sdk));

    if (p_bsp->is_extended)
    {
        p_sdk->id     = FLEXCAN_ID_EXT(p_bsp->id);
        p_sdk->format = (uint8_t) kFLEXCAN_FrameFormatExtend;
    }
    else
    {
        p_sdk->id     = FLEXCAN_ID_STD(p_bsp->id);
        p_sdk->format = (uint8_t) kFLEXCAN_FrameFormatStandard;
    }

    p_sdk->type =
        p_bsp->is_remote ? (uint8_t) kFLEXCAN_FrameTypeRemote : (uint8_t) kFLEXCAN_FrameTypeData;
    p_sdk->length = p_bsp->dlc;

    /* Упаковка data[0..7] → dataWord0/dataWord1 (big-endian byte order). */
    p_sdk->dataByte0 = p_bsp->data[0];
    p_sdk->dataByte1 = p_bsp->data[1];
    p_sdk->dataByte2 = p_bsp->data[2];
    p_sdk->dataByte3 = p_bsp->data[3];
    p_sdk->dataByte4 = p_bsp->data[4];
    p_sdk->dataByte5 = p_bsp->data[5];
    p_sdk->dataByte6 = p_bsp->data[6];
    p_sdk->dataByte7 = p_bsp->data[7];
}

/**
 * Конвертировать flexcan_frame_t → bsp_can_frame_t после RX.
 */
static void frame_from_sdk(const flexcan_frame_t *p_sdk,
                           bsp_can_frame_t *p_bsp) // NOLINT(cppcoreguidelines-avoid-magic-numbers)
{
    (void) memset(p_bsp, 0, sizeof(*p_bsp));

    if ((uint8_t) kFLEXCAN_FrameFormatExtend == p_sdk->format)
    {
        p_bsp->id          = p_sdk->id >> CAN_ID_EXT_SHIFT;
        p_bsp->is_extended = true;
    }
    else
    {
        p_bsp->id          = p_sdk->id >> CAN_ID_STD_SHIFT;
        p_bsp->is_extended = false;
    }

    p_bsp->is_remote = ((uint8_t) kFLEXCAN_FrameTypeRemote == p_sdk->type);
    p_bsp->dlc = (p_sdk->length <= BSP_CAN_DATA_MAX_LEN) ? p_sdk->length : BSP_CAN_DATA_MAX_LEN;

    p_bsp->data[0] = p_sdk->dataByte0;
    p_bsp->data[1] = p_sdk->dataByte1;
    p_bsp->data[2] = p_sdk->dataByte2;
    p_bsp->data[3] = p_sdk->dataByte3;
    p_bsp->data[4] = p_sdk->dataByte4;
    p_bsp->data[5] = p_sdk->dataByte5;
    p_bsp->data[6] = p_sdk->dataByte6;
    p_bsp->data[7] = p_sdk->dataByte7;
}

/**
 * Опросить все активные RX MB и сложить принятые фреймы в ring buffer.
 *
 * @return количество принятых фреймов за этот вызов
 */
static uint32_t poll_rx_mailboxes(void)
{
    uint32_t received = 0U;

    for (uint8_t i = 0U; i < BSP_CAN_FILTER_MAX; i++)
    {
        if ((g_s_rx_mb_active_mask & (1U << i)) == 0U)
        {
            continue;
        }

        uint8_t mb_idx = RX_MB_FIRST + i;

        /* Проверяем флаг готовности MB. */
        uint64_t mb_flag = (uint64_t) 1U << mb_idx;
        if (FLEXCAN_GetMbStatusFlags(BSP_CAN_BASE, mb_flag) == 0U)
        {
            continue;
        }

        /* Читаем фрейм из MB. */
        flexcan_frame_t sdk_frame;
        status_t sdk_status = FLEXCAN_ReadRxMb(BSP_CAN_BASE, mb_idx, &sdk_frame);

        /* Очищаем флаг. */
        FLEXCAN_ClearMbStatusFlags(BSP_CAN_BASE, mb_flag);

        if ((sdk_status == kStatus_Success) || (sdk_status == kStatus_FLEXCAN_RxOverflow))
        {
            bsp_can_frame_t bsp_frame;
            frame_from_sdk(&sdk_frame, &bsp_frame);

            /* Сериализуем фрейм побайтово в ring buffer. */
            ring_buffer_write(&g_s_rx_ring, (const uint8_t *) &bsp_frame, sizeof(bsp_frame));
            received++;
        }
    }

    return received;
}

/**
 * Попробовать извлечь один фрейм из ring buffer.
 *
 * @return true если фрейм получен
 */
static bool try_dequeue_frame(bsp_can_frame_t *p_frame)
{
    if (ring_buffer_count(&g_s_rx_ring) < sizeof(bsp_can_frame_t))
    {
        return false;
    }

    size_t read = ring_buffer_read(&g_s_rx_ring, (uint8_t *) p_frame, sizeof(*p_frame));
    return (read == sizeof(*p_frame));
}

/* ══════════════════════════════════════════════════════════════════════
 *  Публичный API
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Init / Deinit ───────────────────────────────────────────────── */

bsp_status_t bsp_can_init(const bsp_can_config_t *p_config)
{
    if (p_config == NULL)
    {
        return BSP_ERR_PARAM;
    }
    if (p_config->bitrate == 0U || p_config->bitrate > BSP_CAN_BITRATE_MAX)
    {
        return BSP_ERR_PARAM;
    }

    /* Если уже инициализирован — сначала деинициализируем. */
    if (g_s_initialized)
    {
        bsp_can_deinit();
    }

    /* Инициализация ring buffer. */
    ring_buffer_init(&g_s_rx_ring, g_s_rx_ring_storage, RX_RING_SIZE);

    /* Конфигурация FlexCAN. */
    flexcan_config_t flexcan_cfg;
    FLEXCAN_GetDefaultConfig(&flexcan_cfg);

    flexcan_cfg.clkSrc               = BSP_CAN_CLK_SRC;
    flexcan_cfg.bitRate              = p_config->bitrate;
    flexcan_cfg.maxMbNum             = MAX_MB_NUM;
    flexcan_cfg.enableIndividMask    = true;
    flexcan_cfg.disableSelfReception = true;
    flexcan_cfg.enableLoopBack       = false;

    /*
     * Рассчитать оптимальные timing-параметры (propSeg, phaseSeg1/2, rJumpwidth)
     * для заданного bitrate и частоты тактирования.
     * FLEXCAN_Init() сама НЕ вызывает эту функцию — она только подгоняет
     * prescaler по уже заполненному timingConfig. Без явного расчёта
     * timing останется дефолтным (под 1 Mbit/s) и sample point будет неверным.
     */
    flexcan_timing_config_t timing_cfg;
    (void) memset(&timing_cfg, 0, sizeof(timing_cfg));

    if (!FLEXCAN_CalculateImprovedTimingValues(BSP_CAN_BASE, p_config->bitrate, BSP_CAN_CLK_FREQ_HZ,
                                               &timing_cfg))
    {
        return BSP_ERR_PARAM;
    }

    (void) memcpy(&flexcan_cfg.timingConfig, &timing_cfg, sizeof(timing_cfg));

    /* Workaround ERRATA 50235: FLEXCAN_Init() содержит assert который проверяет
     * что CCM_CCGR5_CG12 (LPUART clock gate) открыт когда CAN тактируется
     * от осциллятора. Открываем gate и оставляем открытым — закрывать не нужно,
     * LPUART1 тактируется с минимальным потреблением. */
    CLOCK_EnableClock(kCLOCK_Lpuart1);

    FLEXCAN_Init(BSP_CAN_BASE, &flexcan_cfg, BSP_CAN_CLK_FREQ_HZ);

    /* MB1 — рабочий TX. */
    FLEXCAN_SetTxMbConfig(BSP_CAN_BASE, TX_MB_IDX, true);

    g_s_rx_mb_active_mask = 0U;
    g_s_initialized       = true;

    return BSP_OK;
}

void bsp_can_deinit(void)
{
    if (!g_s_initialized)
    {
        return;
    }

    FLEXCAN_Deinit(BSP_CAN_BASE);

    g_s_rx_mb_active_mask = 0U;
    ring_buffer_reset(&g_s_rx_ring);
    g_s_initialized = false;
}

/* ── Фильтрация ──────────────────────────────────────────────────── */

bsp_status_t bsp_can_set_filter(uint8_t index, uint32_t can_id, uint32_t mask, bool is_extended)
{
    if (!g_s_initialized)
    {
        return BSP_ERR_PARAM;
    }
    if (index >= BSP_CAN_FILTER_MAX)
    {
        return BSP_ERR_PARAM;
    }

    uint8_t mb_idx = RX_MB_FIRST + index;

    /* Конфигурация RX MB. */
    flexcan_rx_mb_config_t rx_mb_cfg;
    rx_mb_cfg.type = kFLEXCAN_FrameTypeData;

    if (is_extended)
    {
        rx_mb_cfg.format = kFLEXCAN_FrameFormatExtend;
        rx_mb_cfg.id     = FLEXCAN_ID_EXT(can_id);
    }
    else
    {
        rx_mb_cfg.format = kFLEXCAN_FrameFormatStandard;
        rx_mb_cfg.id     = FLEXCAN_ID_STD(can_id);
    }

    FLEXCAN_SetRxMbConfig(BSP_CAN_BASE, mb_idx, &rx_mb_cfg, true);

    /* Установить индивидуальную маску.
     * SDK ожидает маску в том же формате, что и ID в MB.
     * Для STD: FLEXCAN_RX_MB_STD_MASK(mask, rtr=0, ide=1)
     *   ide=1 означает: проверять бит IDE (отвергать EXT-фреймы).
     * Для EXT: FLEXCAN_RX_MB_EXT_MASK(mask, rtr=0, ide=1)
     */
    uint32_t sdk_mask;
    if (is_extended)
    {
        sdk_mask = FLEXCAN_RX_MB_EXT_MASK(mask, 0U, 1U);
    }
    else
    {
        sdk_mask = FLEXCAN_RX_MB_STD_MASK(mask, 0U, 1U);
    }

    FLEXCAN_SetRxIndividualMask(BSP_CAN_BASE, mb_idx, sdk_mask);

    g_s_rx_mb_active_mask |= (1U << index);

    return BSP_OK;
}

bsp_status_t bsp_can_accept_all(void)
{
    if (!g_s_initialized)
    {
        return BSP_ERR_PARAM;
    }

    /*
     * Настраиваем два RX MB с маской 0x000
     * (все биты игнорируются — принимает любой ID).
     * Деактивируем остальные.
     */

    /* Деактивировать все ранее настроенные RX MB. */
    for (uint8_t i = 0U; i < BSP_CAN_FILTER_MAX; i++)
    {
        if ((g_s_rx_mb_active_mask & (1U << i)) != 0U)
        {
            FLEXCAN_SetRxMbConfig(BSP_CAN_BASE, RX_MB_FIRST + i, NULL, false);
        }
    }

    g_s_rx_mb_active_mask = 0U;

    /* Настроить MB2 на приём всех STD-фреймов. */
    flexcan_rx_mb_config_t rx_mb_cfg;
    rx_mb_cfg.format = kFLEXCAN_FrameFormatStandard;
    rx_mb_cfg.type   = kFLEXCAN_FrameTypeData;
    rx_mb_cfg.id     = FLEXCAN_ID_STD(0U);

    FLEXCAN_SetRxMbConfig(BSP_CAN_BASE, RX_MB_FIRST, &rx_mb_cfg, true);
    /* STD MB — принимать все STD, отвергать EXT */
    FLEXCAN_SetRxIndividualMask(
        BSP_CAN_BASE, RX_MB_FIRST,
        FLEXCAN_RX_MB_STD_MASK(0U, 0U, 1U)); /* mask=0: любой ID; ide=1: проверять IDE */

    g_s_rx_mb_active_mask = 1U; /* Только index 0 активен. */

    /* Настроить MB3 на приём всех EXT-фреймов. */
    rx_mb_cfg.format = kFLEXCAN_FrameFormatExtend;
    rx_mb_cfg.id     = FLEXCAN_ID_EXT(0U);

    FLEXCAN_SetRxMbConfig(BSP_CAN_BASE, RX_MB_FIRST + 1U, &rx_mb_cfg, true);
    /* EXT MB — принимать все EXT, отвергать STD */
    FLEXCAN_SetRxIndividualMask(
        BSP_CAN_BASE, RX_MB_FIRST + 1U,
        FLEXCAN_RX_MB_EXT_MASK(0U, 0U, 1U)); /* mask=0: любой ID; ide=1: проверять IDE */

    g_s_rx_mb_active_mask |= (1U << 1U); /* index 0 и 1 активны. */

    /* Очистить ring buffer — старые данные неактуальны. */
    ring_buffer_reset(&g_s_rx_ring);

    return BSP_OK;
}

/* ── TX ──────────────────────────────────────────────────────────── */

bsp_status_t bsp_can_send(const bsp_can_frame_t *p_frame, uint32_t timeout_ms)
{
    if (!g_s_initialized)
    {
        return BSP_ERR_PARAM;
    }
    if (p_frame == NULL)
    {
        return BSP_ERR_PARAM;
    }
    if (p_frame->dlc > BSP_CAN_DATA_MAX_LEN)
    {
        return BSP_ERR_PARAM;
    }

    flexcan_frame_t sdk_frame;
    frame_to_sdk(p_frame, &sdk_frame);

    /* Записать фрейм в TX MB. */
    status_t wr_status = FLEXCAN_WriteTxMb(BSP_CAN_BASE, TX_MB_IDX, &sdk_frame);
    if (wr_status != kStatus_Success)
    {
        return BSP_ERR_BUSY;
    }

    /* Ждать завершения передачи с таймаутом. */
    uint64_t tx_flag  = (uint64_t) 1U << TX_MB_IDX;
    uint32_t start_ms = bsp_tick_get_ms();

    while (FLEXCAN_GetMbStatusFlags(BSP_CAN_BASE, tx_flag) == 0U)
    {
        uint32_t elapsed = bsp_tick_get_ms() - start_ms;
        if (elapsed >= timeout_ms)
        {
            return BSP_ERR_TIMEOUT;
        }
    }

    /* Очистить флаг завершения. */
    FLEXCAN_ClearMbStatusFlags(BSP_CAN_BASE, tx_flag);

    return BSP_OK;
}

/* ── RX: polling ─────────────────────────────────────────────────── */

bsp_status_t bsp_can_receive(bsp_can_frame_t *p_frame, uint32_t timeout_ms)
{
    if (!g_s_initialized)
    {
        return BSP_ERR_PARAM;
    }
    if (p_frame == NULL)
    {
        return BSP_ERR_PARAM;
    }

    /* Сначала проверяем — может, в ring buffer уже есть фрейм. */
    if (try_dequeue_frame(p_frame))
    {
        return BSP_OK;
    }

    uint32_t start_ms = bsp_tick_get_ms();

    for (;;)
    {
        /* Опросить все активные MB, сложить в ring buffer. */
        poll_rx_mailboxes();

        /* Попробовать извлечь фрейм. */
        if (try_dequeue_frame(p_frame))
        {
            return BSP_OK;
        }

        /* Проверить таймаут. */
        uint32_t elapsed = bsp_tick_get_ms() - start_ms;
        if (elapsed >= timeout_ms)
        {
            return BSP_ERR_TIMEOUT;
        }
    }
}

/* ── RX: callback (заглушка) ─────────────────────────────────────── */

bsp_status_t bsp_can_register_rx_callback(bsp_can_rx_callback_t p_callback, void *p_user_ctx)
{
    (void) p_callback;
    (void) p_user_ctx;

    return BSP_ERR_NOT_SUPPORTED;
}
/**
 * @file  bsp_mqs.c
 * @brief BSP MQS: SAI3 TX + eDMA + MQS для MIMXRT1052CVJ5B.
 *
 * Тактирование:
 *   Audio PLL     = 24 МГц × (30 + 66/625) = 722.534 МГц
 *   SAI3_CLK_ROOT = Audio PLL / 8 / 8 = 11 289 600 Гц
 *                 (kCLOCK_Sai3Mux=2, Sai3PreDiv=7, Sai3Div=7,
 *                  BOARD_BOOTCLOCKRUN_SAI3_CLK_ROOT)
 *   Bit clock     = 44100 × 16 × 2 = 1 411 200 Гц
 *   MCLK делитель = 11 289 600 / 1 411 200 = 8 (точно, без погрешности)
 *
 * MQS oversample = 32, уже выставлен в BOARD_BootClockRUN() через
 *   IOMUXC_MQSConfig(IOMUXC_GPR, kIOMUXC_MqsPwmOverSampleRate32, 0).
 *
 * Пин: GPIO_AD_B0_04 → MQS_RIGHT — замультиплексирован в BOARD_InitPins().
 *
 * eDMA: DMA0 канал 0, DMAMUX source kDmaRequestMuxSai3Tx.
 *   Канал 0 зарезервирован за bsp_mqs. Прочие модули — каналы 1+.
 *
 * SAI API (SDK 2.4.7 / fsl_sai.h, fsl_sai_edma.h 2.7.3):
 *   Конфигурация через sai_transceiver_t:
 *     SAI_GetClassicI2SConfig() → SAI_TxSetConfig() → SAI_TxSetBitClockRate()
 *   eDMA:
 *     SAI_TransferTxCreateHandleEDMA() → SAI_TransferTxSetConfigEDMA()
 *   FIFO watermark: FSL_FEATURE_SAI_FIFO_COUNTn(x) — макрос с аргументом
 *   экземпляра; требует #include "MIMXRT1052_features.h".
 *   Сброс: SAI_TxReset() (SAI_TxSoftwareReset отсутствует в этой версии).
 */

#include "bsp/mqs.h"

#include "MIMXRT1052_features.h"
#include "clock_config.h"
#include "fsl_clock.h"
#include "fsl_dmamux.h"
#include "fsl_edma.h"
#include "fsl_iomuxc.h"
#include "fsl_sai.h"
#include "fsl_sai_edma.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Конфигурация периферии
 * ----------------------------------------------------------------------- */

#define MQS_SAI_BASE       SAI3
#define MQS_SAI_CLOCK_GATE kCLOCK_Sai3
#define MQS_SAI_CLK_FREQ   BOARD_BOOTCLOCKRUN_SAI3_CLK_ROOT

/** eDMA канал, выделенный под SAI3 TX. */
#define MQS_DMA_CHANNEL (0U)

/** DMAMUX запрос для SAI3 TX. */
#define MQS_DMAMUX_SOURCE kDmaRequestMuxSai3Tx

/** Приоритет прерывания DMA (ниже USB = 3, выше нормальных задач). */
#define MQS_DMA_IRQ_PRIORITY (5U)
#define MQS_HMCLK_GATE       kCLOCK_Mqs
/**
 * FIFO watermark — половина глубины FIFO SAI3.
 * FSL_FEATURE_SAI_FIFO_COUNTn(x) принимает экземпляр SAI и возвращает
 * глубину FIFO в словах (32 для RT1052). Деление на 2 даёт оптимальную
 * латентность DMA: запрос формируется когда в FIFO остаётся место для
 * половины буфера.
 */
#define MQS_SAI_FIFO_WATERMARK                                                                     \
    ((uint8_t) ((uint8_t) FSL_FEATURE_SAI_FIFO_COUNTn(MQS_SAI_BASE) / 2U))

/* --------------------------------------------------------------------------
 * Внутреннее состояние
 * ----------------------------------------------------------------------- */

static bool g_s_initialized       = false;
static volatile bool g_s_dma_done = false;
static volatile bool g_s_busy     = false;

static bsp_mqs_done_cb_t g_s_user_cb = NULL;
static void *g_s_user_data           = NULL;

/*
 * Handles в некэшируемой секции необязательны (не DMA-буферы), но
 * выравнивание по 4 байта обязательно для SDK.
 */
AT_NONCACHEABLE_SECTION_ALIGN(static edma_handle_t s_dma_handle, 4U);
AT_NONCACHEABLE_SECTION_ALIGN(static sai_edma_handle_t s_sai_tx_handle, 4U);

/* --------------------------------------------------------------------------
 * DMA колбэк (ISR-контекст)
 * ----------------------------------------------------------------------- */

static void mqs_edma_callback(I2S_Type *p_base, sai_edma_handle_t *p_handle, status_t status,
                              void *p_user_data)
{
    (void) p_base;
    (void) p_handle;
    (void) status;
    (void) p_user_data;

    g_s_busy     = false;
    g_s_dma_done = true;

    if (g_s_user_cb != NULL)
    {
        g_s_user_cb(g_s_user_data);
    }
}

/* --------------------------------------------------------------------------
 * Публичный API
 * ----------------------------------------------------------------------- */

bsp_status_t bsp_mqs_init(void)
{
    if (g_s_initialized)
    {
        return BSP_OK;
    }

    /* --- Тактирование SAI3 --- */
    CLOCK_EnableClock(MQS_SAI_CLOCK_GATE);

    /* --- Тактирование MQS (CCGR0[CG2]) --- */
    CLOCK_EnableClock(kCLOCK_Mqs);

    /* --- MQS: сброс → включение --- */
    IOMUXC_MQSEnterSoftwareReset(IOMUXC_GPR, true);
    IOMUXC_MQSEnterSoftwareReset(IOMUXC_GPR, false);
    IOMUXC_MQSEnable(IOMUXC_GPR, true);

    /* --- SAI3: базовая инициализация (снимает reset, включает clock gate) --- */
    SAI_Init(MQS_SAI_BASE);

    /* --- SAI3 TX: классический I2S, master, 16 бит, стерео, канал 0. --- */
    sai_transceiver_t sai_cfg;

    SAI_GetLeftJustifiedConfig(&sai_cfg, kSAI_WordWidth16bits, kSAI_Stereo,
                               (uint32_t) kSAI_Channel0Mask);
    sai_cfg.syncMode            = kSAI_ModeAsync;
    sai_cfg.fifo.fifoWatermark  = MQS_SAI_FIFO_WATERMARK;
    sai_cfg.startChannel        = 0;
    sai_cfg.bitClock.bclkSource = kSAI_BclkSourceMclkDiv;

    SAI_TxSetConfig(MQS_SAI_BASE, &sai_cfg);

    /* --- Bit clock rate: вычисляется как sourceClk / (sampleRate * bitWidth * channels).
     *  Функция void — ошибки не возвращает, делитель рассчитывается аппаратно. --- */
    SAI_TxSetBitClockRate(MQS_SAI_BASE, (uint32_t) MQS_SAI_CLK_FREQ,
                          (uint32_t) kSAI_SampleRate44100Hz, (uint32_t) kSAI_WordWidth16bits,
                          BSP_MQS_CHANNELS);

    /* --- eDMA --- */
    edma_config_t dma_cfg;
    EDMA_GetDefaultConfig(&dma_cfg);
    EDMA_Init(DMA0, &dma_cfg);
    EDMA_CreateHandle(&s_dma_handle, DMA0, MQS_DMA_CHANNEL);

    /* --- DMAMUX: канал 0 → SAI3 TX --- */
    DMAMUX_Init(DMAMUX);
    DMAMUX_SetSource(DMAMUX, MQS_DMA_CHANNEL, (uint8_t) MQS_DMAMUX_SOURCE);
    DMAMUX_EnableChannel(DMAMUX, MQS_DMA_CHANNEL);

    /* --- SAI eDMA handle + конфигурация --- */
    SAI_TransferTxCreateHandleEDMA(MQS_SAI_BASE, &s_sai_tx_handle, mqs_edma_callback, NULL,
                                   &s_dma_handle);
    SAI_TransferTxSetConfigEDMA(MQS_SAI_BASE, &s_sai_tx_handle, &sai_cfg);

    SAI_TxEnable(MQS_SAI_BASE, true);

    NVIC_SetPriority(DMA0_DMA16_IRQn, MQS_DMA_IRQ_PRIORITY);

    g_s_busy        = false;
    g_s_dma_done    = false;
    g_s_initialized = true;

    return BSP_OK;
}

void bsp_mqs_deinit(void)
{
    if (!g_s_initialized)
    {
        return;
    }

    SAI_TransferTerminateSendEDMA(MQS_SAI_BASE, &s_sai_tx_handle);
    /* SAI_TxReset() — сброс logic + FIFO (аналог kSAI_ResetAll) для SAI3.
     * SAI_TxSoftwareReset() отсутствует в данной версии SDK. */
    SAI_TxReset(MQS_SAI_BASE);
    IOMUXC_MQSEnable(IOMUXC_GPR, false);
    CLOCK_DisableClock(MQS_SAI_CLOCK_GATE);
    CLOCK_DisableClock(kCLOCK_Mqs);
    g_s_busy        = false;
    g_s_dma_done    = false;
    g_s_user_cb     = NULL;
    g_s_user_data   = NULL;
    g_s_initialized = false;
}

bsp_status_t bsp_mqs_play(const int16_t *p_buf, size_t n_frames, bsp_mqs_done_cb_t p_cb,
                          void *p_user)
{
    if ((p_buf == NULL) || (n_frames == 0U))
    {
        return BSP_ERR_INVALID;
    }
    if (g_s_busy)
    {
        return BSP_ERR_BUSY;
    }

    /* Очищаем возможный Underrun (FEF) от предыдущего проигрывания и сбрасываем FIFO */
    SAI3->TCSR |= I2S_TCSR_FEF_MASK | I2S_TCSR_FR_MASK;

    g_s_user_cb   = p_cb;
    g_s_user_data = p_user;
    g_s_dma_done  = false;
    g_s_busy      = true;

    sai_transfer_t xfer;
    /* p_buf — const int16_t*; SDK ожидает uint8_t*.
     * Cast через uintptr_t: MISRA-compliant ptr→int→ptr. */
    xfer.data     = (uint8_t *) (uintptr_t) p_buf;
    xfer.dataSize = n_frames * (size_t) BSP_MQS_BYTES_PER_FRAME;

    status_t status = SAI_TransferSendEDMA(MQS_SAI_BASE, &s_sai_tx_handle, &xfer);
    if (status != kStatus_Success)
    {
        g_s_busy = false;
        return BSP_ERR_HW;
    }

    return BSP_OK;
}

bsp_status_t bsp_mqs_play_blocking(const int16_t *p_buf, size_t n_frames)
{
    bsp_status_t status = bsp_mqs_play(p_buf, n_frames, NULL, NULL);
    if (status != BSP_OK)
    {
        return status;
    }

    /*
     * Polling-ожидание завершения DMA.
     * Флаг s_dma_done взводится из ISR (mqs_edma_callback).
     * USB keepalive выполняется в вызывающем тест-коде (test_mqs_run).
     */
    while (!g_s_dma_done)
    {
        /* bare-metal: явное ожидание ISR */
    }

    return BSP_OK;
}

void bsp_mqs_stop(void)
{
    if (!g_s_busy)
    {
        return;
    }

    SAI_TransferTerminateSendEDMA(MQS_SAI_BASE, &s_sai_tx_handle);
    g_s_busy     = false;
    g_s_dma_done = false;
    /* Колбэк НЕ вызывается: явная остановка — ответственность вызывающего. */
}

bool bsp_mqs_is_busy(void)
{
    return g_s_busy;
}
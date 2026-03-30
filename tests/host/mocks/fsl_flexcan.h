#pragma once

/**
 * Stub fsl_flexcan.h для host-тестов.
 * Содержит типы, макросы и сигнатуры используемые в:
 *   bsp/can/src/bsp_can.c
 *
 * fff предоставляет реализации через FAKE_* в тестовом файле.
 *
 * Оригинал: sdk/devices/MIMXRT1052/drivers/fsl_flexcan.h
 */

#include "fsl_common.h"

/* ── CAN peripheral ──────────────────────────────────────────────────────── */

typedef struct
{
    volatile uint32_t MCR;
    volatile uint32_t CTRL1;
    volatile uint32_t CTRL2;
    volatile uint32_t TIMER;
    volatile uint32_t ESR1;
    volatile uint32_t IMASK1;
    volatile uint32_t IFLAG1;
    uint32_t reserved[64];
} CAN_Type;

#ifndef CAN2
static CAN_Type g_stub_can2;
#define CAN2 (&g_stub_can2)
#endif

/* ── ID shift/mask constants (from CMSIS device header) ──────────────────── */

#define CAN_ID_STD_SHIFT 18U
#define CAN_ID_STD_MASK  (0x1FFC0000U)
#define CAN_ID_EXT_SHIFT 0U
#define CAN_ID_EXT_MASK  (0x1FFFFFFFU)

/* ── ID and mask helper macros (identical to original SDK) ───────────────── */

#define FLEXCAN_ID_STD(id) (((uint32_t) (((uint32_t) (id)) << CAN_ID_STD_SHIFT)) & CAN_ID_STD_MASK)

#define FLEXCAN_ID_EXT(id)                                                                         \
    (((uint32_t) (((uint32_t) (id)) << CAN_ID_EXT_SHIFT)) & (CAN_ID_EXT_MASK | CAN_ID_STD_MASK))

#define FLEXCAN_RX_MB_STD_MASK(id, rtr, ide)                                                       \
    (((uint32_t) ((uint32_t) (rtr) << 31) | (uint32_t) ((uint32_t) (ide) << 30)) |                 \
     FLEXCAN_ID_STD(id))

#define FLEXCAN_RX_MB_EXT_MASK(id, rtr, ide)                                                       \
    (((uint32_t) ((uint32_t) (rtr) << 31) | (uint32_t) ((uint32_t) (ide) << 30)) |                 \
     FLEXCAN_ID_EXT(id))

/* ── Transfer status codes ───────────────────────────────────────────────── */

enum
{
    kStatus_FLEXCAN_TxBusy     = MAKE_STATUS(kStatusGroup_FLEXCAN, 0),
    kStatus_FLEXCAN_TxIdle     = MAKE_STATUS(kStatusGroup_FLEXCAN, 1),
    kStatus_FLEXCAN_RxBusy     = MAKE_STATUS(kStatusGroup_FLEXCAN, 3),
    kStatus_FLEXCAN_RxIdle     = MAKE_STATUS(kStatusGroup_FLEXCAN, 4),
    kStatus_FLEXCAN_RxOverflow = MAKE_STATUS(kStatusGroup_FLEXCAN, 5),
};

/* ── Enums ───────────────────────────────────────────────────────────────── */

typedef enum _flexcan_frame_format
{
    kFLEXCAN_FrameFormatStandard = 0x0U,
    kFLEXCAN_FrameFormatExtend   = 0x1U,
} flexcan_frame_format_t;

typedef enum _flexcan_frame_type
{
    kFLEXCAN_FrameTypeData   = 0x0U,
    kFLEXCAN_FrameTypeRemote = 0x1U,
} flexcan_frame_type_t;

typedef enum _flexcan_clock_source
{
    kFLEXCAN_ClkSrcOsc  = 0x0U,
    kFLEXCAN_ClkSrcPeri = 0x1U,
    kFLEXCAN_ClkSrc0    = 0x0U,
    kFLEXCAN_ClkSrc1    = 0x1U,
} flexcan_clock_source_t;

/* ── Structs ─────────────────────────────────────────────────────────────── */

typedef struct _flexcan_frame
{
    struct
    {
        uint32_t timestamp : 16;
        uint32_t length : 4;
        uint32_t type : 1;
        uint32_t format : 1;
        uint32_t : 1;
        uint32_t idhit : 9;
    };
    struct
    {
        uint32_t id : 29;
        uint32_t : 3;
    };
    union
    {
        struct
        {
            uint32_t dataWord0;
            uint32_t dataWord1;
        };
        struct
        {
            uint8_t dataByte3;
            uint8_t dataByte2;
            uint8_t dataByte1;
            uint8_t dataByte0;
            uint8_t dataByte7;
            uint8_t dataByte6;
            uint8_t dataByte5;
            uint8_t dataByte4;
        };
    };
} flexcan_frame_t;

typedef struct _flexcan_timing_config
{
    uint16_t preDivider;
    uint8_t rJumpwidth;
    uint8_t phaseSeg1;
    uint8_t phaseSeg2;
    uint8_t propSeg;
} flexcan_timing_config_t;

typedef struct _flexcan_config
{
    uint32_t bitRate;
    flexcan_clock_source_t clkSrc;
    uint8_t maxMbNum;
    bool enableLoopBack;
    bool enableTimerSync;
    bool enableIndividMask;
    bool disableSelfReception;
    bool enableListenOnlyMode;
    bool enableRemoteRequestFrameStored;
    flexcan_timing_config_t timingConfig;
} flexcan_config_t;

typedef struct _flexcan_rx_mb_config
{
    uint32_t id;
    flexcan_frame_format_t format;
    flexcan_frame_type_t type;
} flexcan_rx_mb_config_t;

/* ── Function signatures — реализуются через fff ─────────────────────────── */

void FLEXCAN_GetDefaultConfig(flexcan_config_t *p_config);

bool FLEXCAN_CalculateImprovedTimingValues(CAN_Type *p_base, uint32_t bitrate, uint32_t src_clk_hz,
                                           flexcan_timing_config_t *p_timing_cfg);

void FLEXCAN_Init(CAN_Type *p_base, const flexcan_config_t *p_config, uint32_t src_clk_hz);

void FLEXCAN_Deinit(CAN_Type *p_base);

void FLEXCAN_SetTxMbConfig(CAN_Type *p_base, uint8_t mb_idx, bool enable);

void FLEXCAN_SetRxMbConfig(CAN_Type *p_base, uint8_t mb_idx,
                           const flexcan_rx_mb_config_t *p_rx_mb_config, bool enable);

void FLEXCAN_SetRxIndividualMask(CAN_Type *p_base, uint8_t mask_idx, uint32_t mask);

status_t FLEXCAN_WriteTxMb(CAN_Type *p_base, uint8_t mb_idx, const flexcan_frame_t *p_tx_frame);

status_t FLEXCAN_ReadRxMb(CAN_Type *p_base, uint8_t mb_idx, flexcan_frame_t *p_rx_frame);

uint64_t FLEXCAN_GetMbStatusFlags(CAN_Type *p_base, uint64_t mask);

void FLEXCAN_ClearMbStatusFlags(CAN_Type *p_base, uint64_t mask);
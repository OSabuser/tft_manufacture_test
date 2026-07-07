/*
 * Copyright 2025 MU LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file usb_cdc.c
 * @brief BSP USB CDC ACM — реализация публичного API и NXP callbacks.
 *
 * Архитектура:
 *   - Публичные функции bsp_usb_cdc_*() — вызываются приложением.
 *   - USB_Device*() callbacks — вызываются NXP USB стеком из ISR контекста.
 *   - Данные между ISR и main loop синхронизируются через volatile флаги
 *     и DisableGlobalIRQ/EnableGlobalIRQ (bare-metal critical section).
 *
 * Все DMA-буферы размещены в NonCacheable OCRAM через USB_DMA_*_ALIGN макросы.
 */

#include "bsp/usb_cdc.h"

#include "bsp/tick.h"
#include "fsl_common.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_cdc_acm.h"
#include "usb_device_ch9.h"
#include "usb_device_config.h"
#include "usb_device_descriptor.h"

#include <string.h>

/* ========================================================================
 * Внутренние определения
 * ======================================================================== */

#define CONTROLLER_ID kUSB_ControllerEhci0

/** Размер bulk-буферов. При HS = 512, при FS = 64. Аллоцируем под HS. */
#define DATA_BUFF_SIZE HS_CDC_VCOM_BULK_OUT_PACKET_SIZE

/** Line coding defaults (115200 8N1). */
#define LINE_CODING_DTERATE    (115200U)
#define LINE_CODING_CHARFORMAT (0x00U)
#define LINE_CODING_PARITYTYPE (0x00U)
#define LINE_CODING_DATABITS   (0x08U)
#define LINE_CODING_SIZE       (7U)

#define COMM_FEATURE_DATA_SIZE (2U)
#define STATUS_ABSTRACT_STATE  (0x0000U)
#define COUNTRY_SETTING        (0x0000U)

#define NOTIF_PACKET_SIZE  (8U)
#define UART_BITMAP_SIZE   (2U)
#define NOTIF_REQUEST_TYPE (0xA1U)

/** Задержка после USB_DeviceRun для стабилизации DP pull-down (мкс). */
#define USB_ATTACH_DELAY_US                       (5000U)
#define USB_DEVICE_CDC_REQUEST_SERIAL_STATE_NOTIF (0x20U)
/* ========================================================================
 * Состояние устройства
 * ======================================================================== */

/** Глобальный handle — используется IRQ handler в usb_cdc_hw.c. */
usb_device_handle g_usbDeviceHandle = NULL;

/** Информация о состоянии ACM (serial state notification). */
typedef struct
{
    uint8_t serialStateBuf[NOTIF_PACKET_SIZE + UART_BITMAP_SIZE];
    bool dtePresent;
    uint16_t breakDuration;
    uint8_t dteStatus;
    uint8_t currentInterface;
    uint16_t uartState;
} usb_cdc_acm_info_t;

/** Внутреннее состояние CDC. */
static struct
{
    volatile uint8_t attach;            /**< 1 = USB enumeration done. */
    volatile uint8_t startTransactions; /**< 1 = DTE present, can TX/RX. */
    uint8_t speed;                      /**< USB_SPEED_HIGH / _FULL. */
    uint8_t currentConfiguration;
    uint8_t currentInterfaceAlt[USB_CDC_VCOM_INTERFACE_COUNT];
    uint8_t hasSentState; /**< Serial state notification sent. */
} s_cdcState;

/* ========================================================================
 * DMA-aligned буферы (NonCacheable OCRAM)
 * ======================================================================== */

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_lineCoding[LINE_CODING_SIZE] = {
    (LINE_CODING_DTERATE >> 0U) & 0xFFU,
    (LINE_CODING_DTERATE >> 8U) & 0xFFU,
    (LINE_CODING_DTERATE >> 16U) & 0xFFU,
    (LINE_CODING_DTERATE >> 24U) & 0xFFU,
    LINE_CODING_CHARFORMAT,
    LINE_CODING_PARITYTYPE,
    LINE_CODING_DATABITS,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_abstractState[COMM_FEATURE_DATA_SIZE] = {
    (STATUS_ABSTRACT_STATE >> 0U) & 0xFFU,
    (STATUS_ABSTRACT_STATE >> 8U) & 0xFFU,
};

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_countryCode[COMM_FEATURE_DATA_SIZE] = {
    (COUNTRY_SETTING >> 0U) & 0xFFU,
    (COUNTRY_SETTING >> 8U) & 0xFFU,
};

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static usb_cdc_acm_info_t s_usbCdcAcmInfo;

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_recvBuf[DATA_BUFF_SIZE];

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_sendBuf[DATA_BUFF_SIZE];

USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_setupOutBuffer[8];

/** Количество полученных байт (0 = нет данных, заполняется в ISR). */
static volatile uint32_t s_recvSize = 0U;

/** Количество байт для отправки (0 = idle). */
static volatile uint32_t s_sendSize = 0U;

/** Текущий max packet size (обновляется при смене скорости). */
static uint32_t s_bulkMaxPacketSize = FS_CDC_VCOM_BULK_OUT_PACKET_SIZE;

/** TX transfer завершён (1 = можно отправлять следующий). */
static volatile uint8_t s_txIdle = 1U;

/* ========================================================================
 * Прототипы внешних функций (usb_cdc_hw.c)
 * ======================================================================== */

extern void USB_DeviceClockInit(void);
extern void USB_DeviceIsrEnable(void);

/* ========================================================================
 * USB Endpoint Callbacks (вызываются из ISR контекста)
 * ======================================================================== */

static usb_status_t
USB_DeviceCdcAcmInterruptIn(usb_device_handle handle,
                            usb_device_endpoint_callback_message_struct_t *message,
                            void *callbackParam)
{
    (void) handle;
    (void) message;
    (void) callbackParam;
    s_cdcState.hasSentState = 0U;
    return kStatus_USB_Error;
}

static usb_status_t USB_DeviceCdcAcmBulkIn(usb_device_handle handle,
                                           usb_device_endpoint_callback_message_struct_t *message,
                                           void *callbackParam)
{
    (void) callbackParam;
    usb_status_t error = kStatus_USB_Error;

    if ((message->length != 0U) && (0U == (message->length % s_bulkMaxPacketSize)))
    {
        /* Отправить ZLP чтобы хост понял что передача завершена. */
        error = USB_DeviceSendRequest(handle, USB_CDC_VCOM_BULK_IN_ENDPOINT, NULL, 0U);
    }
    else if ((1U == s_cdcState.attach) && (1U == s_cdcState.startTransactions))
    {
        if ((message->buffer != NULL) || ((message->buffer == NULL) && (message->length == 0U)))
        {
            s_txIdle = 1U;
            /* TX завершён — готовы к следующей отправке. */
            error = kStatus_USB_Success;
        }
    }
    else
    {
        /* no action */
    }

    return error;
}

static usb_status_t USB_DeviceCdcAcmBulkOut(usb_device_handle handle,
                                            usb_device_endpoint_callback_message_struct_t *message,
                                            void *callbackParam)
{
    (void) handle;
    (void) callbackParam;

    if ((1U == s_cdcState.attach) && (1U == s_cdcState.startTransactions))
    {
        s_recvSize = message->length;

        if (0U == s_recvSize)
        {
            /* Хост послал ZLP — сразу планируем следующий приём. */
            USB_DeviceRecvRequest(handle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_recvBuf,
                                  s_bulkMaxPacketSize);
        }

        return kStatus_USB_Success;
    }

    return kStatus_USB_Error;
}

/* ========================================================================
 * USB Device Callbacks (вызываются стеком)
 * ======================================================================== */

usb_status_t USB_DeviceGetSetupBuffer(usb_device_handle handle, usb_setup_struct_t **setupBuffer)
{
    (void) handle;
    static uint32_t s_setupBuf[2];

    if (setupBuffer == NULL)
    {
        return kStatus_USB_InvalidParameter;
    }
    *setupBuffer = (usb_setup_struct_t *) &s_setupBuf;
    return kStatus_USB_Success;
}

usb_status_t USB_DeviceGetClassReceiveBuffer(usb_device_handle handle, usb_setup_struct_t *setup,
                                             uint32_t *length, uint8_t **buffer)
{
    (void) handle;
    (void) setup;

    if ((buffer == NULL) || ((*length) > sizeof(s_setupOutBuffer)))
    {
        return kStatus_USB_InvalidRequest;
    }
    *buffer = s_setupOutBuffer;
    return kStatus_USB_Success;
}

usb_status_t USB_DeviceConfigureRemoteWakeup(usb_device_handle handle, uint8_t enable)
{
    (void) handle;
    (void) enable;
    return kStatus_USB_InvalidRequest;
}

usb_status_t USB_DeviceConfigureEndpointStatus(usb_device_handle handle, uint8_t ep, uint8_t status)
{
    if (status != 0U)
    {
        return USB_DeviceStallEndpoint(handle, ep);
    }
    return USB_DeviceUnstallEndpoint(handle, ep);
}

/* ---- CDC Class-Specific Request Handler ---- */

usb_status_t USB_DeviceProcessClassRequest(usb_device_handle handle, usb_setup_struct_t *setup,
                                           uint32_t *length, uint8_t **buffer)
{
    usb_status_t error          = kStatus_USB_InvalidRequest;
    usb_cdc_acm_info_t *acmInfo = &s_usbCdcAcmInfo;
    uint32_t len;
    uint8_t *uartBitmap;

    if (setup->wIndex != USB_CDC_VCOM_COMM_INTERFACE_INDEX)
    {
        return error;
    }

    switch (setup->bRequest)
    {
    case USB_DEVICE_CDC_REQUEST_SEND_ENCAPSULATED_COMMAND:
    case USB_DEVICE_CDC_REQUEST_GET_ENCAPSULATED_RESPONSE:
    case USB_DEVICE_CDC_REQUEST_CLEAR_COMM_FEATURE:
    case USB_DEVICE_CDC_REQUEST_SEND_BREAK:
        break;

    case USB_DEVICE_CDC_REQUEST_SET_COMM_FEATURE:
        if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_OUT) &&
            (setup->wLength != 0U))
        {
            if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == setup->wValue)
            {
                (void) memcpy(s_abstractState, *buffer, COMM_FEATURE_DATA_SIZE);
                error = kStatus_USB_Success;
            }
            else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == setup->wValue)
            {
                (void) memcpy(s_countryCode, *buffer, COMM_FEATURE_DATA_SIZE);
                error = kStatus_USB_Success;
            }
            else
            {
                /* no action */
            }
        }
        break;

    case USB_DEVICE_CDC_REQUEST_GET_COMM_FEATURE:
        if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_IN) &&
            (setup->wLength != 0U))
        {
            if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == setup->wValue)
            {
                *buffer = s_abstractState;
                *length = COMM_FEATURE_DATA_SIZE;
                error   = kStatus_USB_Success;
            }
            else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == setup->wValue)
            {
                *buffer = s_countryCode;
                *length = COMM_FEATURE_DATA_SIZE;
                error   = kStatus_USB_Success;
            }
            else
            {
                /* no action */
            }
        }
        break;

    case USB_DEVICE_CDC_REQUEST_GET_LINE_CODING:
        if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_IN) &&
            (setup->wLength != 0U))
        {
            *buffer = s_lineCoding;
            *length = LINE_CODING_SIZE;
            error   = kStatus_USB_Success;
        }
        break;

    case USB_DEVICE_CDC_REQUEST_SET_LINE_CODING:
        if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_OUT) &&
            (setup->wLength != 0U))
        {
            (void) memcpy(s_lineCoding, *buffer, LINE_CODING_SIZE);
            error = kStatus_USB_Success;
        }
        break;

    case USB_DEVICE_CDC_REQUEST_SET_CONTROL_LINE_STATE:
    {
        if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_OUT) &&
            (setup->wLength == 0U))
        {
            error              = kStatus_USB_Success;
            acmInfo->dteStatus = setup->wValue;

            /* TX carrier */
            if ((acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_CARRIER_ACTIVATION) != 0U)
            {
                acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
            }
            else
            {
                acmInfo->uartState &= (uint16_t) ~USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
            }

            /* RX carrier (DTE presence) */
            if ((acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE) != 0U)
            {
                acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
            }
            else
            {
                acmInfo->uartState &= (uint16_t) ~USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
            }

            acmInfo->dtePresent =
                ((acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE) != 0U);

            /* Формируем serial state notification */
            acmInfo->serialStateBuf[0] = NOTIF_REQUEST_TYPE;
            acmInfo->serialStateBuf[1] = USB_DEVICE_CDC_REQUEST_SERIAL_STATE_NOTIF;
            acmInfo->serialStateBuf[2] = 0x00U;
            acmInfo->serialStateBuf[3] = 0x00U;
            acmInfo->serialStateBuf[4] = (uint8_t) setup->wIndex;
            acmInfo->serialStateBuf[5] = 0x00U;
            acmInfo->serialStateBuf[6] = UART_BITMAP_SIZE;
            acmInfo->serialStateBuf[7] = 0x00U;

            uartBitmap    = &acmInfo->serialStateBuf[NOTIF_PACKET_SIZE];
            uartBitmap[0] = (uint8_t) (acmInfo->uartState & 0xFFU);
            uartBitmap[1] = (uint8_t) ((acmInfo->uartState >> 8U) & 0xFFU);

            len = (uint32_t) (NOTIF_PACKET_SIZE + UART_BITMAP_SIZE);
            if (0U == s_cdcState.hasSentState)
            {
                error = USB_DeviceSendRequest(handle, USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT,
                                              acmInfo->serialStateBuf, len);
                if (kStatus_USB_Success == error)
                {
                    s_cdcState.hasSentState = 1U;
                }
            }

            if (1U == s_cdcState.attach)
            {
                s_cdcState.startTransactions = 1U;
            }
        }
        break;
    }

    default:
        break;
    }

    return error;
}

/* ---- USB_DeviceCallback (bus reset, set configuration) ---- */

usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    uint8_t *temp8     = (uint8_t *) param;

    switch (event)
    {
    case kUSB_DeviceEventBusReset:
    {
        USB_DeviceControlPipeInit(handle);
        s_cdcState.attach               = 0U;
        s_cdcState.currentConfiguration = 0U;
        s_txIdle                        = 1U;
        s_recvSize                      = 0U;
        s_sendSize                      = 0U;
        error                           = kStatus_USB_Success;

        if (kStatus_USB_Success ==
            USB_DeviceGetStatus(handle, kUSB_DeviceStatusSpeed, &s_cdcState.speed))
        {
            USB_DeviceSetSpeed(handle, s_cdcState.speed);
        }
        break;
    }

    case kUSB_DeviceEventSetConfiguration:
    {
        if (0U == (*temp8))
        {
            s_cdcState.attach               = 0U;
            s_cdcState.currentConfiguration = 0U;
            s_cdcState.startTransactions    = 0U;
            error                           = kStatus_USB_Success;
        }
        else if (USB_CDC_VCOM_CONFIGURE_INDEX == (*temp8))
        {
            usb_device_endpoint_init_struct_t epInit;
            usb_device_endpoint_callback_struct_t epCb;

            s_cdcState.attach               = 1U;
            s_cdcState.currentConfiguration = *temp8;

            /* Interrupt IN endpoint */
            epCb.callbackFn        = USB_DeviceCdcAcmInterruptIn;
            epCb.callbackParam     = handle;
            epInit.zlt             = 0U;
            epInit.transferType    = USB_ENDPOINT_INTERRUPT;
            epInit.endpointAddress = USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT |
                                     (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);

            if (USB_SPEED_HIGH == s_cdcState.speed)
            {
                epInit.maxPacketSize = HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
                epInit.interval      = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
            }
            else
            {
                epInit.maxPacketSize = FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
                epInit.interval      = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
            }
            USB_DeviceInitEndpoint(handle, &epInit, &epCb);

            /* Bulk IN endpoint */
            epCb.callbackFn        = USB_DeviceCdcAcmBulkIn;
            epCb.callbackParam     = handle;
            epInit.zlt             = 0U;
            epInit.interval        = 0U;
            epInit.transferType    = USB_ENDPOINT_BULK;
            epInit.endpointAddress = USB_CDC_VCOM_BULK_IN_ENDPOINT |
                                     (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);

            if (USB_SPEED_HIGH == s_cdcState.speed)
            {
                epInit.maxPacketSize = HS_CDC_VCOM_BULK_IN_PACKET_SIZE;
            }
            else
            {
                epInit.maxPacketSize = FS_CDC_VCOM_BULK_IN_PACKET_SIZE;
            }
            USB_DeviceInitEndpoint(handle, &epInit, &epCb);

            /* Bulk OUT endpoint */
            epCb.callbackFn        = USB_DeviceCdcAcmBulkOut;
            epCb.callbackParam     = handle;
            epInit.zlt             = 0U;
            epInit.interval        = 0U;
            epInit.transferType    = USB_ENDPOINT_BULK;
            epInit.endpointAddress = USB_CDC_VCOM_BULK_OUT_ENDPOINT |
                                     (USB_OUT << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);

            if (USB_SPEED_HIGH == s_cdcState.speed)
            {
                epInit.maxPacketSize = HS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
                s_bulkMaxPacketSize  = HS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
            }
            else
            {
                epInit.maxPacketSize = FS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
                s_bulkMaxPacketSize  = FS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
            }
            USB_DeviceInitEndpoint(handle, &epInit, &epCb);

            /* Планируем первый OUT transfer */
            error = USB_DeviceRecvRequest(handle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_recvBuf,
                                          s_bulkMaxPacketSize);
        }
        else
        {
            /* unknown configuration */
        }
        break;
    }

    case kUSB_DeviceEventSetInterface:
        error = kStatus_USB_Success;
        break;

    default:
        break;
    }

    return error;
}

/* ========================================================================
 * Публичный API (BSP)
 * ======================================================================== */

bsp_status_t bsp_usb_cdc_init(void)
{
    s_cdcState.speed                = USB_SPEED_FULL;
    s_cdcState.attach               = 0U;
    s_cdcState.startTransactions    = 0U;
    s_cdcState.hasSentState         = 0U;
    s_cdcState.currentConfiguration = 0U;
    s_txIdle                        = 1U;
    g_usbDeviceHandle               = NULL;

    USB_DeviceClockInit();

    if (kStatus_USB_Success !=
        USB_DeviceInit(CONTROLLER_ID, USB_DeviceCallback, &g_usbDeviceHandle))
    {
        return BSP_ERR_HW;
    }

    USB_DeviceIsrEnable();

    bsp_delay(USB_ATTACH_DELAY_US / 1000);
    USB_DeviceRun(g_usbDeviceHandle);

    return BSP_OK;
}

bool bsp_usb_cdc_is_ready(void)
{
    return (1U == s_cdcState.attach) && (1U == s_cdcState.startTransactions);
}

bool bsp_usb_cdc_write_ready(void)
{
    return bsp_usb_cdc_is_ready() && (1U == s_txIdle);
}

bsp_status_t bsp_usb_cdc_write(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0U) || (len > DATA_BUFF_SIZE))
    {
        return BSP_ERR_INVALID;
    }

    if (!bsp_usb_cdc_is_ready())
    {
        return BSP_ERR_NOT_READY;
    }

    if (0U == s_txIdle)
    {
        return BSP_ERR_BUSY;
    }

    /* Копируем в NonCacheable TX буфер и запускаем transfer. */
    (void) memcpy(s_sendBuf, data, len);

    s_txIdle = 0U;

    usb_status_t status = USB_DeviceSendRequest(g_usbDeviceHandle, USB_CDC_VCOM_BULK_IN_ENDPOINT,
                                                s_sendBuf, (uint32_t) len);

    if (kStatus_USB_Success != status)
    {
        s_txIdle = 1U;
        return BSP_ERR_HW;
    }

    return BSP_OK;
}

size_t bsp_usb_cdc_read(uint8_t *buf, size_t max_len)
{
    if ((buf == NULL) || (max_len == 0U))
    {
        return 0U;
    }

    if (!bsp_usb_cdc_is_ready())
    {
        return 0U;
    }

    uint32_t sr;
    uint32_t available = 0U;

    sr = DisableGlobalIRQ();
    if ((s_recvSize != 0U) && (s_recvSize != USB_CANCELLED_TRANSFER_LENGTH))
    {
        available  = s_recvSize;
        s_recvSize = 0U;
    }
    EnableGlobalIRQ(sr);

    if (available == 0U)
    {
        return 0U;
    }

    size_t to_copy = (available > (uint32_t) max_len) ? max_len : (size_t) available;
    (void) memcpy(buf, s_recvBuf, to_copy);

    /* Перепланируем приём следующего пакета. */
    USB_DeviceRecvRequest(g_usbDeviceHandle, USB_CDC_VCOM_BULK_OUT_ENDPOINT, s_recvBuf,
                          s_bulkMaxPacketSize);

    return to_copy;
}

void bsp_usb_cdc_poll(void)
{
    /* Bare-metal: NXP USB стек на EHCI обрабатывает всё в ISR.
     * Эта функция зарезервирована для будущего использования
     * (USB_DEVICE_CONFIG_USE_TASK или software event processing). */
}
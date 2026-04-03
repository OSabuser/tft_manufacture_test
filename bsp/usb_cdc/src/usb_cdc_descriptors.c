/*
 * Copyright 2025 MU LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file usb_cdc_descriptors.c
 * @brief USB CDC ACM дескрипторы и descriptor callbacks.
 *
 * Содержит device/configuration/string дескрипторы и функции
 * которые NXP USB стек вызывает для получения дескрипторов при
 * enumeration. Также USB_DeviceSetSpeed() для обновления
 * дескрипторов при переключении HS/FS.
 */

#include "usb.h"
#include "usb_device.h"
#include "usb_device_config.h"
#include "usb_device_descriptor.h"

#include <string.h>

/* ========================================================================
 * Глобальное состояние (используется ch9 callbacks)
 * ======================================================================== */

uint8_t g_currentConfigure = 0U;
uint8_t g_interface[USB_CDC_VCOM_INTERFACE_COUNT];

/* ========================================================================
 * Device Descriptor
 * ======================================================================== */

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceDescriptor[] = {
    USB_DESCRIPTOR_LENGTH_DEVICE,
    USB_DESCRIPTOR_TYPE_DEVICE,
    USB_SHORT_GET_LOW(USB_DEVICE_SPECIFIC_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_SPECIFIC_BCD_VERSION),
    USB_DEVICE_CLASS,
    USB_DEVICE_SUBCLASS,
    USB_DEVICE_PROTOCOL,
    USB_CONTROL_MAX_PACKET_SIZE,
    USB_SHORT_GET_LOW(USB_DEVICE_VID),
    USB_SHORT_GET_HIGH(USB_DEVICE_VID),
    USB_SHORT_GET_LOW(USB_DEVICE_PID),
    USB_SHORT_GET_HIGH(USB_DEVICE_PID),
    USB_SHORT_GET_LOW(USB_DEVICE_DEMO_BCD_VERSION),
    USB_SHORT_GET_HIGH(USB_DEVICE_DEMO_BCD_VERSION),
    0x01U, /* iManufacturer */
    0x02U, /* iProduct */
    0x00U, /* iSerialNumber */
    USB_DEVICE_CONFIGURATION_COUNT,
};

/* ========================================================================
 * Configuration Descriptor (CIC + DIC)
 * ======================================================================== */

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceConfigurationDescriptor[] = {
    /* --- Configuration --- */
    USB_DESCRIPTOR_LENGTH_CONFIGURE,
    USB_DESCRIPTOR_TYPE_CONFIGURE,
    USB_SHORT_GET_LOW(USB_DESCRIPTOR_LENGTH_CONFIGURE + USB_DESCRIPTOR_LENGTH_INTERFACE +
                      USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC + USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG +
                      USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT + USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC +
                      USB_DESCRIPTOR_LENGTH_ENDPOINT + USB_DESCRIPTOR_LENGTH_INTERFACE +
                      USB_DESCRIPTOR_LENGTH_ENDPOINT + USB_DESCRIPTOR_LENGTH_ENDPOINT),
    USB_SHORT_GET_HIGH(USB_DESCRIPTOR_LENGTH_CONFIGURE + USB_DESCRIPTOR_LENGTH_INTERFACE +
                       USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC +
                       USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG + USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT +
                       USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC + USB_DESCRIPTOR_LENGTH_ENDPOINT +
                       USB_DESCRIPTOR_LENGTH_INTERFACE + USB_DESCRIPTOR_LENGTH_ENDPOINT +
                       USB_DESCRIPTOR_LENGTH_ENDPOINT),
    USB_CDC_VCOM_INTERFACE_COUNT,
    USB_CDC_VCOM_CONFIGURE_INDEX,
    0x00U, /* iConfiguration */
    (USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_D7_MASK) |
        (1U << USB_DESCRIPTOR_CONFIGURE_ATTRIBUTE_SELF_POWERED_SHIFT),
    USB_DEVICE_MAX_POWER,

    /* --- CIC: Communication Interface --- */
    USB_DESCRIPTOR_LENGTH_INTERFACE,
    USB_DESCRIPTOR_TYPE_INTERFACE,
    USB_CDC_VCOM_COMM_INTERFACE_INDEX,
    USB_CDC_VCOM_COMM_INTERFACE_ALTERNATE_0,
    USB_CDC_VCOM_ENDPOINT_CIC_COUNT,
    USB_CDC_VCOM_CIC_CLASS,
    USB_CDC_VCOM_CIC_SUBCLASS,
    USB_CDC_VCOM_CIC_PROTOCOL,
    0x00U,

    /* --- CDC Header Functional Descriptor --- */
    USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC,
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE,
    USB_CDC_HEADER_FUNC_DESC,
    0x10U,
    0x01U,

    /* --- CDC Call Management Functional Descriptor --- */
    USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG,
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE,
    USB_CDC_CALL_MANAGEMENT_FUNC_DESC,
    0x01U,
    0x01U,

    /* --- CDC ACM Functional Descriptor --- */
    USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT,
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE,
    USB_CDC_ABSTRACT_CONTROL_FUNC_DESC,
    0x06U,

    /* --- CDC Union Functional Descriptor --- */
    USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC,
    USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE,
    USB_CDC_UNION_FUNC_DESC,
    USB_CDC_VCOM_COMM_INTERFACE_INDEX,
    USB_CDC_VCOM_DATA_INTERFACE_INDEX,

    /* --- Notification Endpoint (Interrupt IN) --- */
    USB_DESCRIPTOR_LENGTH_ENDPOINT,
    USB_DESCRIPTOR_TYPE_ENDPOINT,
    USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_INTERRUPT,
    USB_SHORT_GET_LOW(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE),
    FS_CDC_VCOM_INTERRUPT_IN_INTERVAL,

    /* --- DIC: Data Interface --- */
    USB_DESCRIPTOR_LENGTH_INTERFACE,
    USB_DESCRIPTOR_TYPE_INTERFACE,
    USB_CDC_VCOM_DATA_INTERFACE_INDEX,
    USB_CDC_VCOM_DATA_INTERFACE_ALTERNATE_0,
    USB_CDC_VCOM_ENDPOINT_DIC_COUNT,
    USB_CDC_VCOM_DIC_CLASS,
    USB_CDC_VCOM_DIC_SUBCLASS,
    USB_CDC_VCOM_DIC_PROTOCOL,
    0x00U,

    /* --- Bulk IN Endpoint --- */
    USB_DESCRIPTOR_LENGTH_ENDPOINT,
    USB_DESCRIPTOR_TYPE_ENDPOINT,
    USB_CDC_VCOM_BULK_IN_ENDPOINT | (USB_IN << 7U),
    USB_ENDPOINT_BULK,
    USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_IN_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_IN_PACKET_SIZE),
    0x00U,

    /* --- Bulk OUT Endpoint --- */
    USB_DESCRIPTOR_LENGTH_ENDPOINT,
    USB_DESCRIPTOR_TYPE_ENDPOINT,
    USB_CDC_VCOM_BULK_OUT_ENDPOINT | (USB_OUT << 7U),
    USB_ENDPOINT_BULK,
    USB_SHORT_GET_LOW(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE),
    USB_SHORT_GET_HIGH(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE),
    0x00U,
};

/* ========================================================================
 * String Descriptors
 * ======================================================================== */

USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString0[] = {
    2U + 2U, USB_DESCRIPTOR_TYPE_STRING, 0x09U, 0x04U, /* English (US) */
};

/* Manufacturer: "MU LLC" */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString1[] = {
    2U + 2U * 6U, USB_DESCRIPTOR_TYPE_STRING,
    'M',          0x00U,
    'U',          0x00U,
    ' ',          0x00U,
    'L',          0x00U,
    'L',          0x00U,
    'C',          0x00U,
};

/* Product: "TFT Indicator Board" */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
uint8_t g_UsbDeviceString2[] = {
    2U + 2U * 19U, USB_DESCRIPTOR_TYPE_STRING,
    'T',           0x00U,
    'F',           0x00U,
    'T',           0x00U,
    ' ',           0x00U,
    'I',           0x00U,
    'n',           0x00U,
    'd',           0x00U,
    'i',           0x00U,
    'c',           0x00U,
    'a',           0x00U,
    't',           0x00U,
    'o',           0x00U,
    'r',           0x00U,
    ' ',           0x00U,
    'B',           0x00U,
    'o',           0x00U,
    'a',           0x00U,
    'r',           0x00U,
    'd',           0x00U,
};

/* ========================================================================
 * Language / String List (используется USB_DeviceGetDescriptor)
 * ======================================================================== */
/* Массивы строк и длин — индексируются по (stringIndex - 1) */
static uint8_t *g_UsbDeviceStringDescriptorArray[USB_DEVICE_STRING_COUNT] = {
    g_UsbDeviceString0,
    g_UsbDeviceString1,
    g_UsbDeviceString2,
};

static uint32_t g_UsbDeviceStringDescriptorLength[USB_DEVICE_STRING_COUNT] = {
    sizeof(g_UsbDeviceString0),
    sizeof(g_UsbDeviceString1),
    sizeof(g_UsbDeviceString2),
};

usb_language_t g_UsbDeviceLanguage[USB_DEVICE_LANGUAGE_COUNT] = { {
    g_UsbDeviceStringDescriptorArray,
    g_UsbDeviceStringDescriptorLength,
    (uint16_t) 0x0409U,
} };

usb_language_list_t g_UsbDeviceLanguageList = {
    g_UsbDeviceString0,
    sizeof(g_UsbDeviceString0),
    g_UsbDeviceLanguage,
    USB_DEVICE_LANGUAGE_COUNT,
};

/* ========================================================================
 * Descriptor Callbacks — вызываются NXP USB стеком
 * ======================================================================== */

usb_status_t USB_DeviceGetDescriptor(usb_device_handle handle, usb_setup_struct_t *setup,
                                     uint32_t *length, uint8_t **buffer)
{
    uint8_t descriptorType  = (uint8_t) ((setup->wValue & 0xFF00U) >> 8U);
    uint8_t descriptorIndex = (uint8_t) ((setup->wValue & 0x00FFU));
    usb_status_t ret        = kStatus_USB_Success;

    if (USB_REQUEST_STANDARD_GET_DESCRIPTOR != setup->bRequest)
    {
        return kStatus_USB_InvalidRequest;
    }

    switch (descriptorType)
    {
    case USB_DESCRIPTOR_TYPE_STRING:
    {
        if (descriptorIndex == 0U)
        {
            *buffer = (uint8_t *) g_UsbDeviceLanguageList.languageString;
            *length = g_UsbDeviceLanguageList.stringLength;
        }
        else
        {
            uint8_t langId = USB_DEVICE_LANGUAGE_COUNT;

            for (uint8_t i = 0U; i < USB_DEVICE_LANGUAGE_COUNT; i++)
            {
                if (setup->wIndex == g_UsbDeviceLanguageList.languageList[i].languageId)
                {
                    langId = i;
                    break;
                }
            }

            if (USB_DEVICE_LANGUAGE_COUNT == langId)
            {
                ret = kStatus_USB_InvalidRequest;
                break;
            }

            uint8_t strIdx = descriptorIndex - 1U;
            if (strIdx < USB_DEVICE_STRING_COUNT)
            {
                *buffer = (uint8_t *) g_UsbDeviceLanguageList.languageList[langId].string[strIdx];
                *length = g_UsbDeviceLanguageList.languageList[langId].length[strIdx];
            }
            else
            {
                ret = kStatus_USB_InvalidRequest;
            }
        }
        break;
    }
    case USB_DESCRIPTOR_TYPE_DEVICE:
    {
        *buffer = g_UsbDeviceDescriptor;
        *length = USB_DESCRIPTOR_LENGTH_DEVICE;
        break;
    }
    case USB_DESCRIPTOR_TYPE_CONFIGURE:
    {
        *buffer = g_UsbDeviceConfigurationDescriptor;
        *length = USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL;
        break;
    }
    default:
        ret = kStatus_USB_InvalidRequest;
        break;
    }

    return ret;
}

/* ---- Chapter 9: Set/Get Configure/Interface ---- */

usb_status_t USB_DeviceSetConfigure(usb_device_handle handle, uint8_t cfg)
{
    if (cfg == 0U)
    {
        return kStatus_USB_Error;
    }
    g_currentConfigure = cfg;
    return USB_DeviceCallback(handle, kUSB_DeviceEventSetConfiguration, &cfg);
}

usb_status_t USB_DeviceGetConfigure(usb_device_handle handle, uint8_t *cfg)
{
    *cfg = g_currentConfigure;
    return kStatus_USB_Success;
}

usb_status_t USB_DeviceSetInterface(usb_device_handle handle, uint8_t iface, uint8_t alt)
{
    if (iface < USB_CDC_VCOM_INTERFACE_COUNT)
    {
        g_interface[iface] = alt;
        return USB_DeviceCallback(handle, kUSB_DeviceEventSetInterface, &iface);
    }
    return kStatus_USB_InvalidRequest;
}

usb_status_t USB_DeviceGetInterface(usb_device_handle handle, uint8_t iface, uint8_t *alt)
{
    if (iface < USB_CDC_VCOM_INTERFACE_COUNT)
    {
        *alt = g_interface[iface];
        return kStatus_USB_Success;
    }
    return kStatus_USB_InvalidRequest;
}

/* ---- USB_DeviceSetSpeed ---- */

usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed)
{
    usb_descriptor_union_t *ptr1;
    usb_descriptor_union_t *ptr2;

    ptr1 = (usb_descriptor_union_t *) (&g_UsbDeviceConfigurationDescriptor[0]);
    ptr2 =
        (usb_descriptor_union_t
             *) (&g_UsbDeviceConfigurationDescriptor[USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL - 1U]);

    while (ptr1 < ptr2)
    {
        if (ptr1->common.bDescriptorType == USB_DESCRIPTOR_TYPE_ENDPOINT)
        {
            /* Interrupt IN */
            if (((ptr1->endpoint.bEndpointAddress &
                  USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                 USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                ((ptr1->endpoint.bEndpointAddress & 0x0FU) == USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT))
            {
                if (USB_SPEED_HIGH == speed)
                {
                    ptr1->endpoint.bInterval = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else
                {
                    ptr1->endpoint.bInterval = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
            }
            /* Bulk IN */
            else if (((ptr1->endpoint.bEndpointAddress &
                       USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                      USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_IN) &&
                     ((ptr1->endpoint.bEndpointAddress & 0x0FU) == USB_CDC_VCOM_BULK_IN_ENDPOINT))
            {
                if (USB_SPEED_HIGH == speed)
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_IN_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
            }
            /* Bulk OUT */
            else if (((ptr1->endpoint.bEndpointAddress &
                       USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK) ==
                      USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_OUT) &&
                     ((ptr1->endpoint.bEndpointAddress & 0x0FU) == USB_CDC_VCOM_BULK_OUT_ENDPOINT))
            {
                if (USB_SPEED_HIGH == speed)
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(HS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
                else
                {
                    USB_SHORT_TO_LITTLE_ENDIAN_ADDRESS(FS_CDC_VCOM_BULK_OUT_PACKET_SIZE,
                                                       ptr1->endpoint.wMaxPacketSize);
                }
            }
            else
            {
                /* other endpoints — no action */
            }
        }
        ptr1 = (usb_descriptor_union_t *) ((uint8_t *) ptr1 + ptr1->common.bLength);
    }

    return kStatus_USB_Success;
}
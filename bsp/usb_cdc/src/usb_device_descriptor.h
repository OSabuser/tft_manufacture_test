/*
 * Copyright 2025 TFT Project
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file usb_device_descriptor.h
 * @brief USB CDC ACM дескрипторы — константы и прототипы.
 *
 * Определяет VID/PID, номера эндпоинтов, размеры пакетов.
 * Используется только внутри bsp_usb_cdc.
 */

#ifndef USB_DEVICE_DESCRIPTOR_H_
#define USB_DEVICE_DESCRIPTOR_H_

/* ---- Версии USB -------------------------------------------------------- */
#define USB_DEVICE_SPECIFIC_BCD_VERSION (0x0200U)
#define USB_DEVICE_DEMO_BCD_VERSION     (0x0101U)

/* ---- VID / PID --------------------------------------------------------- */
#define USB_DEVICE_VID (0x1996U)
#define USB_DEVICE_PID (0x00ADU)

/* ---- CDC коды классов -------------------------------------------------- */
#define CDC_COMM_CLASS (0x02U)
#define CDC_DATA_CLASS (0x0AU)

/* ---- CDC SubClass / Protocol ------------------------------------------- */
#define USB_CDC_ABSTRACT_CONTROL_MODEL     (0x02U)
#define USB_CDC_NO_CLASS_SPECIFIC_PROTOCOL (0x00U)

/* ---- Functional Descriptor SubTypes ------------------------------------ */
#define USB_CDC_HEADER_FUNC_DESC           (0x00U)
#define USB_CDC_CALL_MANAGEMENT_FUNC_DESC  (0x01U)
#define USB_CDC_ABSTRACT_CONTROL_FUNC_DESC (0x02U)
#define USB_CDC_UNION_FUNC_DESC            (0x06U)

/* ---- Длины функциональных дескрипторов --------------------------------- */
#define USB_DESCRIPTOR_LENGTH_CDC_HEADER_FUNC (5U)
#define USB_DESCRIPTOR_LENGTH_CDC_CALL_MANAG  (5U)
#define USB_DESCRIPTOR_LENGTH_CDC_ABSTRACT    (4U)
#define USB_DESCRIPTOR_LENGTH_CDC_UNION_FUNC  (5U)

/* ---- CS Descriptor Types ----------------------------------------------- */
#define USB_DESCRIPTOR_TYPE_CDC_CS_INTERFACE (0x24U)
#define USB_DESCRIPTOR_TYPE_CDC_CS_ENDPOINT  (0x25U)

/* ---- Конфигурация, интерфейсы, эндпоинты ------------------------------- */
#define USB_DEVICE_CONFIGURATION_COUNT (1U)
#define USB_DEVICE_STRING_COUNT        (3U)
#define USB_DEVICE_LANGUAGE_COUNT      (1U)

#define USB_CDC_VCOM_CONFIGURE_INDEX (1U)

#define USB_CDC_VCOM_ENDPOINT_CIC_COUNT (1U)
#define USB_CDC_VCOM_ENDPOINT_DIC_COUNT (2U)

#define USB_CDC_VCOM_INTERRUPT_IN_ENDPOINT (1U)
#define USB_CDC_VCOM_BULK_IN_ENDPOINT      (2U)
#define USB_CDC_VCOM_BULK_OUT_ENDPOINT     (3U)

#define USB_CDC_VCOM_INTERFACE_COUNT      (2U)
#define USB_CDC_VCOM_COMM_INTERFACE_INDEX (0U)
#define USB_CDC_VCOM_DATA_INTERFACE_INDEX (1U)

/* ---- Размеры пакетов --------------------------------------------------- */
#define HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE (16U)
#define FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE (16U)
#define HS_CDC_VCOM_INTERRUPT_IN_INTERVAL    (0x07U)
#define FS_CDC_VCOM_INTERRUPT_IN_INTERVAL    (0x08U)

#define HS_CDC_VCOM_BULK_IN_PACKET_SIZE  (512U)
#define FS_CDC_VCOM_BULK_IN_PACKET_SIZE  (64U)
#define HS_CDC_VCOM_BULK_OUT_PACKET_SIZE (512U)
#define FS_CDC_VCOM_BULK_OUT_PACKET_SIZE (64U)

/* ---- Device class codes ------------------------------------------------ */
#define USB_DEVICE_CLASS     (0x02U)
#define USB_DEVICE_SUBCLASS  (0x00U)
#define USB_DEVICE_PROTOCOL  (0x00U)
#define USB_DEVICE_MAX_POWER (0x32U)

/* ---- CIC / DIC class codes -------------------------------------------- */
#define USB_CDC_VCOM_CIC_CLASS    (CDC_COMM_CLASS)
#define USB_CDC_VCOM_CIC_SUBCLASS (USB_CDC_ABSTRACT_CONTROL_MODEL)
#define USB_CDC_VCOM_CIC_PROTOCOL (USB_CDC_NO_CLASS_SPECIFIC_PROTOCOL)
#define USB_CDC_VCOM_DIC_CLASS    (CDC_DATA_CLASS)
#define USB_CDC_VCOM_DIC_SUBCLASS (0x00U)
#define USB_CDC_VCOM_DIC_PROTOCOL (USB_CDC_NO_CLASS_SPECIFIC_PROTOCOL)

/* ---- Alternate settings ------------------------------------------------ */
#define USB_CDC_VCOM_COMM_INTERFACE_ALTERNATE_COUNT (1U)
#define USB_CDC_VCOM_DATA_INTERFACE_ALTERNATE_COUNT (1U)
#define USB_CDC_VCOM_COMM_INTERFACE_ALTERNATE_0     (0U)
#define USB_CDC_VCOM_DATA_INTERFACE_ALTERNATE_0     (0U)

/* ---- String descriptor lengths (computed from arrays) ------------------ */
#define USB_DESCRIPTOR_LENGTH_STRING0           (sizeof(g_UsbDeviceString0))
#define USB_DESCRIPTOR_LENGTH_STRING1           (sizeof(g_UsbDeviceString1))
#define USB_DESCRIPTOR_LENGTH_STRING2           (sizeof(g_UsbDeviceString2))
#define USB_DESCRIPTOR_LENGTH_CONFIGURATION_ALL (sizeof(g_UsbDeviceConfigurationDescriptor))

/* ---- Прототипы (реализация в usb_cdc.c) -------------------------------- */

/**
 * @brief Обработка USB device событий (bus reset, set config и т.д.).
 */
extern usb_status_t USB_DeviceCallback(usb_device_handle handle, uint32_t event, void *param);

/**
 * @brief Обновление дескрипторов при смене скорости HS/FS.
 */
extern usb_status_t USB_DeviceSetSpeed(usb_device_handle handle, uint8_t speed);

#endif /* USB_DEVICE_DESCRIPTOR_H_ */
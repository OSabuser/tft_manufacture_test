/*
 * Copyright 2025 TFT Project
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file usb_device_config.h
 * @brief Конфигурация NXP USB Device Stack для bsp_usb_cdc.
 *
 * Этот файл — application-specific конфиг, который NXP USB middleware
 * подключает через include path. Не является частью SDK.
 *
 * Настройки под bare-metal CDC ACM на USB1 (EHCI0).
 */
#ifndef USB_DEVICE_CONFIG_H_
#define USB_DEVICE_CONFIG_H_

/* ---- Контроллер -------------------------------------------------------- */
#define USB_DEVICE_CONFIG_EHCI        (1U)
#define USB_DEVICE_CONFIG_KHCI        (0U)
#define USB_DEVICE_CONFIG_LPCIP3511FS (0U)
#define USB_DEVICE_CONFIG_LPCIP3511HS (0U)

/** Суммарное количество активных контроллеров. */
#define USB_DEVICE_CONFIG_NUM                                                                      \
    (USB_DEVICE_CONFIG_EHCI + USB_DEVICE_CONFIG_KHCI + USB_DEVICE_CONFIG_LPCIP3511FS +             \
     USB_DEVICE_CONFIG_LPCIP3511HS)

/* ---- EHCI-специфичные -------------------------------------------------- */
#define USB_DEVICE_CONFIG_EHCI_MAX_DTD (16U)

/* ---- Классы устройства ------------------------------------------------- */
#define USB_DEVICE_CONFIG_CDC_ACM   (1U)
#define USB_DEVICE_CONFIG_HID       (0U)
#define USB_DEVICE_CONFIG_MSC       (0U)
#define USB_DEVICE_CONFIG_AUDIO     (0U)
#define USB_DEVICE_CONFIG_VIDEO     (0U)
#define USB_DEVICE_CONFIG_PHDC      (0U)
#define USB_DEVICE_CONFIG_DFU       (0U)
#define USB_DEVICE_CONFIG_PRINTER   (0U)
#define USB_DEVICE_CONFIG_CCID      (0U)
#define USB_DEVICE_CONFIG_MTP       (0U)
#define USB_DEVICE_CONFIG_CDC_ECM   (0U)
#define USB_DEVICE_CONFIG_CDC_RNDIS (0U)

/* ---- Общие настройки --------------------------------------------------- */
#define USB_DEVICE_CONFIG_ENDPOINTS    (4U)
#define USB_DEVICE_CONFIG_SELF_POWER   (1U)
#define USB_DEVICE_CONFIG_MAX_MESSAGES (8U)
#define USB_DEVICE_CONFIG_ROOT2_TEST   (0U)

#endif /* USB_DEVICE_CONFIG_H_ */
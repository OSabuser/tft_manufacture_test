/*
 * Copyright 2025 MU LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file usb_cdc_hw.c
 * @brief Аппаратная инициализация USB1 (EHCI0): clock, PHY, IRQ.
 *
 * Функции вызываются из usb_cdc.c при инициализации.
 * IRQ handler делегирует обработку в NXP USB device stack.
 */

#include "board.h"
#include "clock_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_config.h"
#include "usb_phy.h"

/* --------------------------------------------------------------------------
 * Определения платы (из board.h / schematic)
 * -------------------------------------------------------------------------- */

/** D_CAL, TXCAL45DP, TXCAL45DM — калибровка USB PHY. */
#define BOARD_USB_PHY_D_CAL     (0x0CU)
#define BOARD_USB_PHY_TXCAL45DP (0x06U)
#define BOARD_USB_PHY_TXCAL45DM (0x06U)

/** Приоритет USB прерывания (ниже числовое значение = выше приоритет). */
#define USB_DEVICE_INTERRUPT_PRIORITY (3U)

/** Контроллер: USB1 = kUSB_ControllerEhci0. */
#define CONTROLLER_ID kUSB_ControllerEhci0

/* --------------------------------------------------------------------------
 * Внешний символ — handle USB device, определён в usb_cdc.c
 * -------------------------------------------------------------------------- */
extern usb_device_handle g_usbDeviceHandle;

/* --------------------------------------------------------------------------
 * IRQ Handler
 * -------------------------------------------------------------------------- */

/**
 * @brief USB OTG1 IRQ — делегирует в NXP EHCI ISR.
 *
 * Имя совпадает с вектором прерывания в startup_MIMXRT1052.S.
 * Внутри — только вызов SDK функции, без блокировки и аллокаций.
 */
void USB_OTG1_IRQHandler(void)
{
    USB_DeviceEhciIsrFunction(g_usbDeviceHandle);
}

/* --------------------------------------------------------------------------
 * Clock + PHY
 * -------------------------------------------------------------------------- */

/**
 * @brief Включить тактирование USB1 PHY 480 MHz и инициализировать PHY.
 *
 * Вызывается один раз из bsp_usb_cdc_init() до USB_DeviceInit().
 */
void USB_DeviceClockInit(void)
{
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
    CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, 480000000U);

    USB_EhciPhyInit(CONTROLLER_ID, BOARD_XTAL0_CLK_HZ, &phyConfig);
}

/* --------------------------------------------------------------------------
 * NVIC
 * -------------------------------------------------------------------------- */

/**
 * @brief Разрешить USB1 прерывание в NVIC.
 *
 * Вызывается один раз из bsp_usb_cdc_init() после USB_DeviceInit().
 */
void USB_DeviceIsrEnable(void)
{
    uint8_t irqNumber;
    uint8_t usbDeviceEhciIrq[] = USBHS_IRQS;

    irqNumber = usbDeviceEhciIrq[CONTROLLER_ID - kUSB_ControllerEhci0];

    NVIC_SetPriority((IRQn_Type) irqNumber, USB_DEVICE_INTERRUPT_PRIORITY);
    EnableIRQ((IRQn_Type) irqNumber);
}
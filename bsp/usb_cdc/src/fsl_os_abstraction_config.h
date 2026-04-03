/*
 * Copyright 2025 TFT Project
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @file fsl_os_abstraction_config.h
 * @brief Конфигурация OSA bare-metal для bsp_usb_cdc.
 *
 * NXP OSA подключает этот файл через include path.
 * Настройки под bare-metal (без FreeRTOS).
 */
#ifndef FSL_OS_ABSTRACTION_CONFIG_H_
#define FSL_OS_ABSTRACTION_CONFIG_H_

#define gMainThreadStackSize_c             (1024U)
#define gMainThreadPriority_c              (7U)
#define gTaskMultipleInstancesManagement_c (0U)
#define FSL_OSA_ALLOCATED_HEAP             (1U)

#endif /* FSL_OS_ABSTRACTION_CONFIG_H_ */
/**
 * @file  sysflash.h
 * @brief Отображение логических слотов bootutil на flash-area ID.
 *
 * MCUBOOT_IMAGE_NUMBER=1 → два ID: Slot A (primary=0), Slot Б (secondary=1).
 * Без scratch — Direct-XIP не использует область подкачки. Смещения/размеры
 * самих областей заданы в flash_map_backend.c (см.
 * docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md).
 */

#ifndef SYSFLASH_H_
#define SYSFLASH_H_

#include "mcuboot_config/mcuboot_config.h"

#define FLASH_AREA_IMAGE_PRIMARY(x)   (((x) == 0) ? 0 : 255)
#define FLASH_AREA_IMAGE_SECONDARY(x) (((x) == 0) ? 1 : 255)

#define MCUBOOT_IMAGE_SLOT_NUMBER (MCUBOOT_IMAGE_NUMBER * 2)

/** @brief Единственное flash-устройство в системе — W25Qxx через FlexSPI. */
#define FLASH_DEVICE_ID 1

int flash_area_id_from_multi_image_slot(int image_index, int slot);

#endif /* SYSFLASH_H_ */

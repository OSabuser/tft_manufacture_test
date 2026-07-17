/**
 * @file  flash_map.h
 * @brief Контракт bootutil на "область флеша" — реализуется
 *        flash_map_backend.c (реальный, над bsp_qspi_flash) или
 *        fake_flash_map_backend.c (host-тесты, in-memory буфер).
 *
 */

#ifndef FLASH_MAP_H_
#define FLASH_MAP_H_

#include <stdint.h>

/**
 * @brief Область на flash-устройстве.
 *
 * Несколько устройств в системе не предполагается (см. FLASH_DEVICE_ID в
 * sysflash.h) — fa_device_id всегда равен FLASH_DEVICE_ID.
 */
struct flash_area
{
    uint8_t fa_id;        /**< ID области, уникален в системе. */
    uint8_t fa_device_id; /**< ID flash-устройства. */
    uint16_t pad16;
    uint32_t fa_off; /**< Смещение области от начала устройства. */
    uint32_t fa_size; /**< Размер области, байт. */
};

/** @brief Сектор внутри области (смещение относительно начала области). */
struct flash_sector
{
    uint32_t fs_off;
    uint32_t fs_size;
};

/** @brief Базовый адрес flash-устройства в адресном пространстве MCU (XIP). */
int flash_device_base(uint8_t fd_id, uintptr_t *ret);

int flash_area_open(uint8_t id, const struct flash_area **area);
void flash_area_close(const struct flash_area *area);

/* Read/write/erase — смещение относительно начала области. */
int flash_area_read(const struct flash_area *area, uint32_t off, void *dst, uint32_t len);
int flash_area_write(const struct flash_area *area, uint32_t off, const void *src, uint32_t len);
int flash_area_erase(const struct flash_area *area, uint32_t off, uint32_t len);

/** @brief Минимальное выравнивание записи. */
uint8_t flash_area_align(const struct flash_area *area);

/** @brief Значение стёртого байта (0xFF для NOR). */
uint8_t flash_area_erased_val(const struct flash_area *area);

/** @brief Прочитать len байт с off и проверить что это стёртая область. */
int flash_area_read_is_empty(const struct flash_area *area, uint32_t off, void *dst, uint32_t len);

int flash_area_get_sectors(int fa_id, uint32_t *count, struct flash_sector *sectors);
int flash_area_get_sector(const struct flash_area *fa, uint32_t off, struct flash_sector *sector);

int flash_area_id_from_image_slot(int slot);
int flash_area_id_to_multi_image_slot(int image_index, int area_id);

#endif /* FLASH_MAP_H_ */

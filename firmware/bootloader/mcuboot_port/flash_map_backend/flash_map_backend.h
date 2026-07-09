/**
 * @file  flash_map_backend.h
 * @brief Инлайн-аксессоры flash_area/flash_sector, требуемые bootutil.
 */

#ifndef FLASH_MAP_BACKEND_H_
#define FLASH_MAP_BACKEND_H_

#include "flash_map.h"

static inline uint8_t flash_area_get_id(const struct flash_area *fa)
{
    return fa->fa_id;
}

static inline uint8_t flash_area_get_device_id(const struct flash_area *fa)
{
    return fa->fa_device_id;
}

static inline uint32_t flash_area_get_off(const struct flash_area *fa)
{
    return fa->fa_off;
}

static inline uint32_t flash_area_get_size(const struct flash_area *fa)
{
    return fa->fa_size;
}

static inline uint32_t flash_sector_get_off(const struct flash_sector *fs)
{
    return fs->fs_off;
}

static inline uint32_t flash_sector_get_size(const struct flash_sector *fs)
{
    return fs->fs_size;
}

#endif /* FLASH_MAP_BACKEND_H_ */

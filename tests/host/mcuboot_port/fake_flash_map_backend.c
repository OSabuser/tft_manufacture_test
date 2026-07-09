/**
 * @file  fake_flash_map_backend.c
 * @brief Реализация flash_map.h поверх статического буфера в памяти хоста.
 *        См. fake_flash_map_backend.h.
 */

#include "fake_flash_map_backend.h"

#include "flash_map.h"
#include "sysflash/sysflash.h"

#include <string.h>

#define ERASED_VAL 0xFFU

/** @brief Slot A на [0, FAKE_FLASH_SLOT_SIZE), Slot Б сразу за ним. */
static uint8_t g_s_flash_buf[2U * FAKE_FLASH_SLOT_SIZE];

static const struct flash_area g_s_areas[2] = {
    { .fa_id = 0U, .fa_device_id = FLASH_DEVICE_ID, .pad16 = 0U,
      .fa_off = 0U, .fa_size = FAKE_FLASH_SLOT_SIZE },
    { .fa_id = 1U, .fa_device_id = FLASH_DEVICE_ID, .pad16 = 0U,
      .fa_off = FAKE_FLASH_SLOT_SIZE, .fa_size = FAKE_FLASH_SLOT_SIZE },
};

/* ── Test-only helpers ────────────────────────────────────────────────── */

void fake_flash_reset(void)
{
    memset(g_s_flash_buf, ERASED_VAL, sizeof(g_s_flash_buf));
}

void fake_flash_write_slot(int slot_idx, const uint8_t *p_data, size_t len)
{
    uint8_t *p_dst = g_s_flash_buf + ((size_t) slot_idx * FAKE_FLASH_SLOT_SIZE);
    memcpy(p_dst, p_data, len);
}

/* ── flash_map.h contract ─────────────────────────────────────────────── */

int flash_device_base(uint8_t fd_id, uintptr_t *ret)
{
    if (fd_id != FLASH_DEVICE_ID)
    {
        return -1;
    }
    *ret = (uintptr_t) g_s_flash_buf;
    return 0;
}

int flash_area_open(uint8_t id, const struct flash_area **area)
{
    if (id >= 2U)
    {
        return -1;
    }
    *area = &g_s_areas[id];
    return 0;
}

void flash_area_close(const struct flash_area *area)
{
    (void) area;
}

int flash_area_read(const struct flash_area *area, uint32_t off, void *dst, uint32_t len)
{
    if (off + len > area->fa_size)
    {
        return -1;
    }
    memcpy(dst, g_s_flash_buf + area->fa_off + off, len);
    return 0;
}

int flash_area_write(const struct flash_area *area, uint32_t off, const void *src, uint32_t len)
{
    if (off + len > area->fa_size)
    {
        return -1;
    }
    memcpy(g_s_flash_buf + area->fa_off + off, src, len);
    return 0;
}

int flash_area_erase(const struct flash_area *area, uint32_t off, uint32_t len)
{
    if (off + len > area->fa_size)
    {
        return -1;
    }
    memset(g_s_flash_buf + area->fa_off + off, ERASED_VAL, len);
    return 0;
}

uint8_t flash_area_align(const struct flash_area *area)
{
    (void) area;
    return 1U;
}

uint8_t flash_area_erased_val(const struct flash_area *area)
{
    (void) area;
    return ERASED_VAL;
}

int flash_area_read_is_empty(const struct flash_area *area, uint32_t off, void *dst, uint32_t len)
{
    if (flash_area_read(area, off, dst, len) != 0)
    {
        return -1;
    }

    const uint8_t *p_buf = (const uint8_t *) dst;
    for (uint32_t i = 0U; i < len; i++)
    {
        if (p_buf[i] != ERASED_VAL)
        {
            return 0;
        }
    }
    return 1;
}

int flash_area_get_sector(const struct flash_area *fa, uint32_t off, struct flash_sector *sector)
{
    if (off >= fa->fa_size)
    {
        return -1;
    }
    sector->fs_off  = (off / FAKE_FLASH_SECTOR_SIZE) * FAKE_FLASH_SECTOR_SIZE;
    sector->fs_size = FAKE_FLASH_SECTOR_SIZE;
    return 0;
}

int flash_area_get_sectors(int fa_id, uint32_t *count, struct flash_sector *sectors)
{
    const struct flash_area *fa;
    uint32_t max_cnt = *count;

    if (flash_area_open((uint8_t) fa_id, &fa) != 0)
    {
        return -1;
    }

    uint32_t rem_len = fa->fa_size;
    *count           = 0U;
    while ((rem_len > 0U) && (*count < max_cnt))
    {
        sectors[*count].fs_off  = FAKE_FLASH_SECTOR_SIZE * (*count);
        sectors[*count].fs_size = FAKE_FLASH_SECTOR_SIZE;
        (*count)++;
        rem_len -= FAKE_FLASH_SECTOR_SIZE;
    }

    return 0;
}

int flash_area_id_from_multi_image_slot(int image_index, int slot)
{
    switch (slot)
    {
    case 0:
        return FLASH_AREA_IMAGE_PRIMARY(image_index);
    case 1:
        return FLASH_AREA_IMAGE_SECONDARY(image_index);
    default:
        return -1;
    }
}

int flash_area_id_from_image_slot(int slot)
{
    return flash_area_id_from_multi_image_slot(0, slot);
}

int flash_area_id_to_multi_image_slot(int image_index, int area_id)
{
    if (area_id == FLASH_AREA_IMAGE_PRIMARY(image_index))
    {
        return 0;
    }
    if (area_id == FLASH_AREA_IMAGE_SECONDARY(image_index))
    {
        return 1;
    }
    return -1;
}

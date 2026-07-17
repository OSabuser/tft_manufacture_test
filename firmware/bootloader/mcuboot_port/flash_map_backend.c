/**
 * @file  flash_map_backend.c
 * @brief Реализация flash_map.h поверх bsp_qspi_flash — Slot A/Б из
 *        docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md.
 *
 * bsp_qspi_read/write_page/erase_sector() принимают flash-relative адрес
 * (0-based от начала чипа, IPCR0 FlexSPI IP-команд) — НЕ XIP-адрес
 * (0x60000000+). fa_off здесь — то же самое flash-relative смещение.
 * flash_device_base() — единственное место, где встречается XIP-адрес
 * 0x60000000: он нужен boot_select.c для вычисления адреса прыжка
 * (flash_base + fa_off + hdr_size), но не самим read/write/erase.
 *
 * @pre bsp_qspi_init() должен быть вызван до любой flash_area_* функции
 *      (main.c, до boot_go()).
 */

#include "bsp/qspi_flash.h"
#include "bsp/wdog.h"
#include "flash_map.h"
#include "led_status.h"
#include "sysflash/sysflash.h"

#include <string.h>

#define ERASED_VAL 0xFFU

/* Slot A/Б — см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md. Смещения —
 * flash-relative (от начала чипа), не XIP-адрес. */
static const struct flash_area g_s_areas[2] = {
    { .fa_id        = 0U,
      .fa_device_id = FLASH_DEVICE_ID,
      .pad16        = 0U,
      .fa_off       = 0x00040000UL,
      .fa_size      = 0x00200000UL }, /* Slot A: 0x60040000, 2 МБ */
    { .fa_id        = 1U,
      .fa_device_id = FLASH_DEVICE_ID,
      .pad16        = 0U,
      .fa_off       = 0x00240000UL,
      .fa_size      = 0x00200000UL }, /* Slot Б: 0x60240000, 2 МБ */
};

/* ── Постраничная запись (аналог NXP flash_area_write_internal) ─────────
 *
 * bsp_qspi_write_page() пишет ровно BSP_QSPI_PAGE_SIZE (256) байт по
 * странично-выровненному адресу. Запись 0xFF поверх уже запрограммированных
 * байт — не изменяет их (NOR program может только сбрасывать биты 1→0,
 * запись 0xFF не запрашивает сброс ни одного бита) — поэтому безопасно
 * "перезатирать" уже записанную часть страницы буфером, где нетронутая
 * часть заполнена ERASED_VAL: bootutil пишет монотонно возрастающими
 * смещениями, повторно данные не перезаписывает.
 */
static int write_page_chunked(uint32_t dst_addr, const uint8_t *p_src, uint32_t len)
{
    uint8_t page_buf[BSP_QSPI_PAGE_SIZE];

    uint32_t chunk_ofs  = dst_addr % BSP_QSPI_PAGE_SIZE;
    uint32_t page_addr  = dst_addr - chunk_ofs;
    uint32_t chunk_size = BSP_QSPI_PAGE_SIZE - chunk_ofs;

    while (len > 0U)
    {
        if (chunk_size > len)
        {
            chunk_size = len;
        }

        memset(page_buf, ERASED_VAL, BSP_QSPI_PAGE_SIZE);
        memcpy(page_buf + chunk_ofs, p_src, chunk_size);

        if (bsp_qspi_write_page(page_addr, page_buf) != BSP_OK)
        {
            return -1;
        }

        p_src += chunk_size;
        len -= chunk_size;
        chunk_ofs  = 0U;
        chunk_size = BSP_QSPI_PAGE_SIZE;
        page_addr += BSP_QSPI_PAGE_SIZE;
    }

    return 0;
}

/* ── flash_map.h contract ─────────────────────────────────────────────── */

int flash_device_base(uint8_t fd_id, uintptr_t *ret)
{
    if (fd_id != FLASH_DEVICE_ID)
    {
        return -1;
    }
    *ret = 0x60000000UL; /* XIP-mapped база — только для вычисления адреса прыжка */
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
    return (bsp_qspi_read(area->fa_off + off, (uint8_t *) dst, len) == BSP_OK) ? 0 : -1;
}

int flash_area_write(const struct flash_area *area, uint32_t off, const void *src, uint32_t len)
{
    if (off + len > area->fa_size)
    {
        return -1;
    }
    return write_page_chunked(area->fa_off + off, (const uint8_t *) src, len);
}

int flash_area_erase(const struct flash_area *area, uint32_t off, uint32_t len)
{
    if ((off + len > area->fa_size) || ((off % BSP_QSPI_SECTOR_SIZE) != 0U) ||
        ((len % BSP_QSPI_SECTOR_SIZE) != 0U))
    {
        return -1;
    }

    /* Fast-path: стирание всей области целиком (off=0, len=fa_size), кратно
     * 64 КБ — блочное стирание ~5x быстрее посекторного (2 МБ: ~4.8 с против
     * ~23 с, см. docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md, "Обоснование
     * размеров"). Ускоряет и sd_update (Фаза 3), и штатный revert-erase
     * bootutil (boot_select_or_erase() в loader.c) — они уже зовут
     * flash_area_erase(fap, 0, flash_area_get_size(fap)) без изменений.
     * Частичное/невыровненное стирание (напр. один трейлер) — прежний
     * посекторный путь ниже. */
    if ((off == 0U) && (len == area->fa_size) && ((len % BSP_QSPI_BLOCK_64K_SIZE) == 0U))
    {
        uint32_t block_addr = area->fa_off;
        for (; len > 0U; len -= BSP_QSPI_BLOCK_64K_SIZE)
        {
            /* Кормим watchdog поблочно: стирание 2 МБ ~4.8 c — это реальный
             * прогресс, но один блочный вызов не должен упереться в таймаут.
             * Зависание самого стирания флеша всё равно ловится: refresh — по
             * ЗАВЕРШЕНИИ блока, а не перед ним. */
            bsp_wdog_refresh();
            /* Прогресс-хук индикации — no-op вне окна установки (см.
             * led_status.h). Нужен, чтобы APP не замирал на ~5 c стирания слота
             * при установке; на revert/recovery-стирании (тоже зовут эту
             * функцию) ничего не рисует. */
            led_status_tick_install();
            if (bsp_qspi_erase_block_64k(block_addr) != BSP_OK)
            {
                return -1;
            }
            block_addr += BSP_QSPI_BLOCK_64K_SIZE;
        }
        return 0;
    }

    uint32_t addr = area->fa_off + off;
    for (; len > 0U; len -= BSP_QSPI_SECTOR_SIZE)
    {
        bsp_wdog_refresh(); /* см. выше — посекторный путь тоже длинный */
        if (bsp_qspi_erase_sector(addr) != BSP_OK)
        {
            return -1;
        }
        addr += BSP_QSPI_SECTOR_SIZE;
    }

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
    sector->fs_off  = (off / BSP_QSPI_SECTOR_SIZE) * BSP_QSPI_SECTOR_SIZE;
    sector->fs_size = BSP_QSPI_SECTOR_SIZE;
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
        sectors[*count].fs_off  = BSP_QSPI_SECTOR_SIZE * (*count);
        sectors[*count].fs_size = BSP_QSPI_SECTOR_SIZE;
        (*count)++;
        rem_len -= BSP_QSPI_SECTOR_SIZE;
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

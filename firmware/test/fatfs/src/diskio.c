/*
 * diskio.c — FatFS diskio диспетчер для firmware_test.
 *
 * FF_VOLUMES=3: диски 0 и 1 — заглушки, диск 2 = SDDISK (microSD).
 * W25Q и RAM-диск отсутствуют — нет зависимости на bsp_qspi_flash.
 */

#include "diskio.h"

#include "port/fatfs/diskio_sd.h"

#define SDDISK 2U

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv == SDDISK)
    {
        return microsd_disk_initialize(pdrv);
    }

    return STA_NOINIT;
}

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv == SDDISK)
    {
        return microsd_disk_status(pdrv);
    }

    return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == SDDISK)
    {
        return microsd_disk_read(pdrv, buff, sector, count);
    }

    return RES_PARERR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == SDDISK)
    {
        return microsd_disk_write(pdrv, buff, sector, count);
    }

    return RES_PARERR;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv == SDDISK)
    {
        return microsd_disk_ioctl(pdrv, cmd, buff);
    }

    return RES_PARERR;
}
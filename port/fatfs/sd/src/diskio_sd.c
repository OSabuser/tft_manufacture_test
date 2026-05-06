/*
 * diskio_sd — тонкая обёртка fsl_sd_disk → FatFS diskio для microSD.
 *
 * fsl_sd_disk.c из SDK уже реализует sd_disk_* поверх fsl_sd / USDHC.
 * Наша задача — только пробросить вызовы и изолировать номер диска:
 * sd_disk_* всегда работают с диском 0 внутри себя, pdrv мы передаём
 * для соблюдения сигнатуры, но fsl_sd_disk игнорирует его значение.
 */

#include "port/fatfs/diskio_sd.h"

#include "fsl_sd_disk.h"

DSTATUS microsd_disk_initialize(BYTE pdrv)
{
    return sd_disk_initialize(pdrv);
}

DSTATUS microsd_disk_status(BYTE pdrv)
{
    return sd_disk_status(pdrv);
}

DRESULT microsd_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    return sd_disk_read(pdrv, buff, sector, count);
}

DRESULT microsd_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    return sd_disk_write(pdrv, buff, sector, count);
}

DRESULT microsd_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    return sd_disk_ioctl(pdrv, cmd, buff);
}
/*
 * diskio_sd — реализация FatFS diskio-интерфейса для microSD через fsl_sd_disk.
 *
 * Функции microsd_disk_* вызываются диспетчером diskio.c каждого бинарника.
 * Никогда не вызывать напрямую из прикладного кода.
 */

#ifndef PORT_FATFS_DISKIO_SD_H
#define PORT_FATFS_DISKIO_SD_H

#include "diskio.h" /* DSTATUS, DRESULT, BYTE, LBA_t, UINT */
#include "ff.h"

DSTATUS microsd_disk_initialize(BYTE pdrv);
DSTATUS microsd_disk_status(BYTE pdrv);
DRESULT microsd_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT microsd_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT microsd_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#endif /* PORT_FATFS_DISKIO_SD_H */
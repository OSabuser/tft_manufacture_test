/*
 * ffconf.h — конфигурация FatFS для firmware_test (bare-metal, только SD).
 *
 * Ключевые отличия от tft_app:
 *   FF_FS_REENTRANT = 0  — нет RTOS, нет мьютексов
 *   FF_VOLUMES      = 3  —  0: зарезервирован, 1: зарезервирован, 2: SD 
 *   FF_MAX_SS       = 512 — SD всегда 512 байт/сектор, ioctl не нужен
 */

#ifndef _FFCONF_H_
#define _FFCONF_H_

#define FFCONF_DEF 80286

/*---------------------------------------------------------------------------/
/ MSDK adaptation
/---------------------------------------------------------------------------*/
#define SD_DISK_ENABLE 1

/*---------------------------------------------------------------------------/
/ Function Configurations
/---------------------------------------------------------------------------*/
#define FF_FS_READONLY  0
#define FF_FS_MINIMIZE  0
#define FF_USE_FIND     0
#define FF_USE_MKFS     0 /* f_mkfs не нужна — карта уже отформатирована */
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND   0
#define FF_USE_CHMOD    0
#define FF_USE_LABEL    0
#define FF_USE_FORWARD  0
#define FF_USE_STRFUNC  0
#define FF_PRINT_LLI    0
#define FF_PRINT_FLOAT  0
#define FF_STRF_ENCODE  3

/*---------------------------------------------------------------------------/
/ Locale
/---------------------------------------------------------------------------*/
#define FF_CODE_PAGE 437 /* U.S. — минимальный, имена файлов ASCII */

#define FF_USE_LFN     0 /* только 8.3 — достаточно для FWTEST.TMP */
#define FF_MAX_LFN     255
#define FF_LFN_UNICODE 0
#define FF_LFN_BUF     255
#define FF_SFN_BUF     12
#define FF_FS_RPATH    0 /* относительные пути не нужны */

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/
#define FF_VOLUMES         3 /* 0: зарезервирован, 1: зарезервирован, 2: SD */
#define FF_STR_VOLUME_ID   0
#define FF_MULTI_PARTITION 0

#define FF_MIN_SS 512
#define FF_MAX_SS 512 /* SD: всегда 512, GET_SECTOR_SIZE не нужен */

#define FF_LBA64    0
#define FF_MIN_GPT  0x10000000
#define FF_USE_TRIM 0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/
#define FF_FS_TINY     0
#define FF_FS_EXFAT    0 /* exFAT требует LFN — оба отключены */
#define FF_FS_NORTC    1
#define FF_NORTC_MON   1
#define FF_NORTC_MDAY  1
#define FF_NORTC_YEAR  2024
#define FF_FS_NOFSINFO 0
#define FF_FS_LOCK     0

#define FF_FS_REENTRANT 0 /* bare-metal: нет RTOS, нет мьютексов */
                          /* FF_FS_TIMEOUT и FF_SYNC_t не нужны   */

#endif /* _FFCONF_H_ */
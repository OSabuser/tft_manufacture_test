/*
 * sdmmc_config.h — board-level конфигурация SDMMC для MIMXRT1052CVJ5B.
 *
 * SD_CD : GPIO_B1_12 → USDHC1.usdhc_cd_b (периферийный сигнал, не GPIO).
 *         Детект через USDHC_GetPresentStatusFlags → kSD_DetectCardByHostCD.
 *         GPIO-прерывание на CD не нужно.
 *
 * SD_PWR: GPIO_AD_B1_03 → GPIO1[19] (SdPwr).
 *         Active-high: 1 = питание включено.
 */

#ifndef BSP_SDMMC_CONFIG_H
#define BSP_SDMMC_CONFIG_H

#include "clock_config.h"
#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_sdmmc_common.h"
#include "fsl_sdmmc_host.h"

#ifdef SD_ENABLED
#include "fsl_sd.h"
#endif

/* ---------------------------------------------------------------------------
 * Host controller
 * ------------------------------------------------------------------------- */
#define BOARD_SDMMC_SD_HOST_BASEADDR     USDHC1
#define BOARD_SDMMC_SD_HOST_IRQ          USDHC1_IRQn
#define BOARD_SDMMC_SD_HOST_IRQ_PRIORITY 5U

/* ---------------------------------------------------------------------------
 * Card detect
 * CD_B подключён как периферийный сигнал USDHC1 — детект через регистр
 * PRSSTAT, а не через GPIO. Прерывание и callback на CD не используются.
 * ------------------------------------------------------------------------- */
#define BOARD_SDMMC_SD_CD_TYPE        kSD_DetectCardByHostCD
#define BOARD_SDMMC_SD_CD_DEBOUNCE_MS 100U

/* ---------------------------------------------------------------------------
 * Card power — GPIO1[19] (GPIO_AD_B1_03, SdPwr), active-high
 * ------------------------------------------------------------------------- */
#define BOARD_SDMMC_SD_PWR_GPIO_BASE GPIO1
#define BOARD_SDMMC_SD_PWR_GPIO_PIN  19U

/* ---------------------------------------------------------------------------
 * IO voltage — управляется хостом (LDO внутри USDHC)
 * ------------------------------------------------------------------------- */
#define BOARD_SDMMC_SD_IO_VOLTAGE_TYPE kSD_IOVoltageCtrlByHost

/* ---------------------------------------------------------------------------
 * DMA и кэш
 * ------------------------------------------------------------------------- */
#define BOARD_SDMMC_HOST_DMA_DESCRIPTOR_BUFFER_SIZE 32U
#define BOARD_SDMMC_DATA_BUFFER_ALIGN_SIZE          32U
#define BOARD_SDMMC_HOST_CACHE_CONTROL              kSDMMCHOST_CacheControlRWBuffer

/* ---------------------------------------------------------------------------
 * Максимальная частота (SDR104)
 * ------------------------------------------------------------------------- */
#define BOARD_SDMMC_SD_HOST_SUPPORT_SDR104_FREQ 200000000U

#if defined(__cplusplus)
extern "C"
{
#endif

#ifdef SD_ENABLED
    /*
 * Конфигурирует sd_card_t: host, CD, power, IO voltage, частота.
 * cd и userData передаются NULL из bsp_sd — CD управляется хостом.
 */
    void BOARD_SD_Config(void *card, sd_cd_t cd, uint32_t host_irq_priority, void *user_data);
#endif

#if defined(__cplusplus)
}
#endif

#endif /* BSP_SDMMC_CONFIG_H */
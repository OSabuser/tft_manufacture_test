/*
 * bsp_sd — реализация.
 */

#include "bsp/sd.h"

#include "fsl_sd.h"
#include "fsl_usdhc.h"
#include "sdmmc_config.h"

#include <string.h>

extern sd_card_t g_sd;

/* ---------------------------------------------------------------------------
 * Состояние модуля
 * ------------------------------------------------------------------------- */
static bool g_s_host_configured = false;
static bool g_s_initialized     = false;

/* ---------------------------------------------------------------------------
 * Внутренние функции
 * ------------------------------------------------------------------------- */

/*
 * Конфигурирует SDMMC host однократно.
 * Повторный вызов — no-op: BOARD_SD_Config не идемпотентен сам по себе
 * (переинициализирует GPIO питания), поэтому защищаем флагом.
 */
static void ensure_host_configured(void)
{
    if (g_s_host_configured)
    {
        return;
    }
    /* cd=NULL, userData=NULL: детект — GPIO-callback внутри BOARD_SD_Config(),
     * не внешний callback сюда (см. bsp_sd_is_inserted() — тот же механизм). */
    BOARD_SD_Config(&g_sd, NULL, BOARD_SDMMC_SD_HOST_IRQ_PRIORITY, NULL);
    g_s_host_configured = true;
}

/* ---------------------------------------------------------------------------
 * Публичный API
 * ------------------------------------------------------------------------- */

bsp_status_t bsp_sd_init(void)
{
    if (g_s_initialized)
    {
        return BSP_OK;
    }

    /*
     * Аппаратный сброс USDHC FIFO + command/data state machine перед
     * повторной инициализацией.
     */
    CLOCK_EnableClock(kCLOCK_Usdhc1); /* тактирование нужно ДО сброса регистров */
    USDHC_Reset(BOARD_SDMMC_SD_HOST_BASEADDR, kUSDHC_ResetAll, 100U);

    /*
     * Обнулить g_sd целиком перед повторной конфигурацией. BOARD_SD_Config()
     * перезаписывает только часть полей — указатели на callback-структуры
     * non-blocking host driver и внутренние DMA-дескрипторы могут остаться
     * от предыдущей сессии, если их явно не сбросить.
     */
    (void) memset(&g_sd, 0, sizeof(g_sd));
    g_s_host_configured = false; /* форсируем повторный BOARD_SD_Config ниже */

    ensure_host_configured(); /* BOARD_SD_Config — заполняет g_sd, включая usrParam.pwr */

    if (SD_HostInit(&g_sd) != kStatus_Success)
    {
        return BSP_ERR_HW;
    }

    if (SD_PollingCardInsert(&g_sd, kSD_Inserted) != kStatus_Success)
    {
        return BSP_ERR_HW;
    }

    SD_SetCardPower(&g_sd, false);
    SD_SetCardPower(&g_sd, true);

    g_s_initialized = true;
    return BSP_OK;
}

bsp_status_t bsp_sd_deinit(void)
{
    if (!g_s_initialized)
    {
        return BSP_OK;
    }

    SD_HostDeinit(&g_sd);
    SD_SetCardPower(&g_sd, false);

    USDHC_Reset(BOARD_SDMMC_SD_HOST_BASEADDR, kUSDHC_ResetAll, 100U);

    g_s_initialized     = false;
    g_s_host_configured = false;
    return BSP_OK;
}

bool bsp_sd_is_inserted(void)
{
    CLOCK_EnableClock(kCLOCK_Usdhc1);

    uint32_t ps = USDHC_GetPresentStatusFlags(BOARD_SDMMC_SD_HOST_BASEADDR);
    return (ps & kUSDHC_CardInsertedFlag) != 0U;
}
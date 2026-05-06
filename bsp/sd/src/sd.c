/*
 * bsp_sd — реализация.
 */

#include "bsp/sd.h"

#include "fsl_clock.h"
#include "fsl_sd.h"
#include "sdmmc_config.h" /* BOARD_SD_Config, BOARD_SDMMC_SD_HOST_BASEADDR */

/* ---------------------------------------------------------------------------
 * Глобальный дескриптор карты — нужен SDK-стеку (передаётся по указателю
 * в BOARD_SD_Config и sd_disk_initialize через g_sd).
 * Объявлен без static — fsl_sd_disk.c ссылается на него как extern sd_card_t g_sd.
 * ------------------------------------------------------------------------- */
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
    /* cd=NULL, userData=NULL: CD управляется хостом через PRSSTAT */
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

    ensure_host_configured(); /* только BOARD_SD_Config — заполняет g_sd */

    /* 
     * Полный init (host + card) происходит в sd_disk_initialize → SD_Init,
     * который вызывается из f_mount → disk_initialize.
     */

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

    g_s_initialized = false;
    return BSP_OK;
}

bool bsp_sd_is_inserted(void)
{
    /*
     * CD_B подключён как периферийный сигнал USDHC1, поэтому детект —
     * через регистр PRSSTAT, а не GPIO.
     *
     * USDHC1 может быть ещё не тактирован в момент вызова (до init),
     * поэтому явно включаем clock. После init clock уже включён SDK-стеком,
     * повторный вызов CLOCK_EnableClock безвреден.
     */
    CLOCK_EnableClock(kCLOCK_Usdhc1);
    uint32_t prsstat = USDHC_GetPresentStatusFlags(BOARD_SDMMC_SD_HOST_BASEADDR);
    return (prsstat & (uint32_t) kUSDHC_CardInsertedFlag) != 0U;
}
/*
 * bsp_sd — реализация.
 */

#include "bsp/sd.h"

#include "fsl_sd.h"
#include "fsl_usdhc.h"    /* USDHC_Reset — аппаратный сброс FIFO/state machine */
#include "sdmmc_config.h" /* BOARD_SD_Config, BOARD_SDMMC_SD_HOST_BASEADDR */

#include <string.h> /* memset */
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

    /*
     * Аппаратный сброс USDHC FIFO + command/data state machine ПЕРЕД
     * повторной инициализацией. Без этого non-blocking host driver SDK
     * (fsl_sdmmc_host.c) может остаться в состоянии "ожидание завершения
     * предыдущей транзакции" после SD_HostDeinit() на прошлом прогоне —
     * физическая транзакция уже умерла вместе с deinit, но внутренний
     * флаг ожидания interrupt остаётся выставленным, и следующий f_mount()
     * блокируется навсегда в ожидании события, которое никогда не придёт.
     *
     * USDHC_Reset с маской kUSDHC_ResetAll сбрасывает контроллер на
     * регистровом уровне, не полагаясь на состояние, оставленное
     * предыдущей сессией. Безопасно вызывать даже при первом запуске —
     * базовый адрес уже доступен через BOARD_SDMMC_SD_HOST_BASEADDR
     * (clock на этот момент должен быть включён, см. ниже).
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

    /*
     * Дополнительный аппаратный сброс сразу после deinit — гарантирует,
     * что FIFO и state machine USDHC не останутся в промежуточном
     * состоянии независимо от того, что делает (или не делает)
     * SD_HostDeinit() из SDK на уровне регистров.
     */
    USDHC_Reset(BOARD_SDMMC_SD_HOST_BASEADDR, kUSDHC_ResetAll, 100U);

    g_s_initialized     = false;
    g_s_host_configured = false;
    return BSP_OK;
}

bool bsp_sd_is_inserted(void)
{
    return GPIO_PinRead(BOARD_SDMMC_SD_CD_GPIO_BASE, BOARD_SDMMC_SD_CD_GPIO_PIN) ==
           BOARD_SDMMC_SD_CD_INSERT_LEVEL;
}

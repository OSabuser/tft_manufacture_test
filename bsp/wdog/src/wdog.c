/*
 * bsp_wdog — реализация поверх fsl_wdog (WDOG1).
 */

#include "bsp/wdog.h"

#include "fsl_wdog.h"

#define BSP_WDOG_BASE WDOG1

/* Границы поля WCR.WT (8 бит): таймаут = (WT+1) * 0.5 c, максимум 128 c. */
#define BSP_WDOG_TIMEOUT_S_MIN 1U
#define BSP_WDOG_TIMEOUT_S_MAX 128U

static bool g_s_armed               = false;
static bool g_s_last_reset_was_wdog = false;
static uint32_t g_s_timeout_s       = 0U;

bsp_status_t bsp_wdog_init(uint32_t timeout_s)
{
    if (g_s_armed)
    {
        return BSP_OK; /* WDE — write-once; повторно не взводим */
    }

    if ((timeout_s < BSP_WDOG_TIMEOUT_S_MIN) || (timeout_s > BSP_WDOG_TIMEOUT_S_MAX))
    {
        return BSP_ERR_PARAM;
    }

    /*
     * Причина предыдущего сброса: читаем WDOG1->WRSR до настройки. WRSR
     * read-only, отражает последний сброс (TOUT=WDOG-таймаут, POR=power-on),
     * стабилен до следующего сброса.
     */
    g_s_last_reset_was_wdog = (BSP_WDOG_BASE->WRSR & WDOG_WRSR_TOUT_MASK) != 0U;

    wdog_config_t cfg;
    WDOG_GetDefaultConfig(&cfg);

    /* WT = 2*timeout_s - 1 → таймаут = (WT+1)*0.5 c = timeout_s c. */
    cfg.timeoutValue = (uint16_t) ((timeout_s * 2U) - 1U);

    /*
     * КРИТИЧНО для рабочего процесса: не сбрасывать плату, когда ядро
     * остановлено отладчиком (SWD halt) — иначе пошаговая отладка загрузчика
     * невозможна. enableWait/enableStop оставляем как в дефолте: загрузчик и
     * приложение в эти режимы не входят, но если войдут — пусть watchdog
     * продолжает считать (безопаснее по умолчанию).
     */
    cfg.workMode.enableDebug = false;

    cfg.enableWdog = true;
    WDOG_Init(BSP_WDOG_BASE, &cfg); /* с этого момента WDE взведён навсегда */

    g_s_timeout_s = timeout_s;
    g_s_armed     = true;
    return BSP_OK;
}

void bsp_wdog_refresh(void)
{
    WDOG_Refresh(BSP_WDOG_BASE);
}

bool bsp_wdog_caused_last_reset(void)
{
    return g_s_last_reset_was_wdog;
}

bool bsp_wdog_is_armed(void)
{
    return g_s_armed;
}

uint32_t bsp_wdog_timeout_s(void)
{
    return g_s_timeout_s;
}

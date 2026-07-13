/*
 * bsp_boot_state — реализация поверх fsl_src (SRC_GPR5 + SRC->SRSR).
 */

#include "bsp/boot_state.h"

#include "fsl_src.h"

#define BSP_BOOT_STATE_SRC_BASE SRC

/*
 * GPR-индекс счётчика попыток (0-based, index=0 -> GPR1). Сверено с i.MX RT1050
 * RM (SRC, гл. 21.8.5-21.8.13): GPR1/2 — ROM (entry/arg пробуждения из
 * low-power), GPR6/7/8/9 — ТОЖЕ explicit "used by the ROM code, should not be
 * used by application software" (несмотря на первоначальное предположение,
 * что свободны только GPR1/2/10), GPR10 — ROM (альтернативный SBMR1 через бит
 * [28]). GPR5 формально не ROM, но RM рекомендует именно его под отдельную
 * задачу (различение SYSRESETREQ/CPU lockup) — не занимаем во избежание
 * конфликта с этой конвенцией. GPR3 (index 2) — единственный не подписан
 * НИКАКИМ примечанием в RM, описан просто как "arbitrary value".
 */
#define BSP_BOOT_STATE_GPR_INDEX 2U

static bool g_s_was_por = false;

void bsp_boot_state_init(void)
{
    uint32_t flags = SRC_GetResetStatusFlags(BSP_BOOT_STATE_SRC_BASE);

    g_s_was_por = (flags & (uint32_t) kSRC_IppResetPinFlag) != 0U;

    /* SRSR — write-1-to-clear, копит биты между тёплыми сбросами без явной
     * очистки (в отличие от WDOG1->WRSR, который самоочищается). Чистим всё,
     * что доступно, чтобы следующая загрузка увидела только свою причину. */
    SRC_ClearResetStatusFlags(BSP_BOOT_STATE_SRC_BASE, ~0U);

    if (g_s_was_por)
    {
        bsp_boot_attempt_reset();
    }
}

bool bsp_boot_state_was_por(void)
{
    return g_s_was_por;
}

uint32_t bsp_boot_attempt_count(void)
{
    return SRC_GetGeneralPurposeRegister(BSP_BOOT_STATE_SRC_BASE, BSP_BOOT_STATE_GPR_INDEX);
}

void bsp_boot_attempt_inc(void)
{
    uint32_t count = bsp_boot_attempt_count();
    SRC_SetGeneralPurposeRegister(BSP_BOOT_STATE_SRC_BASE, BSP_BOOT_STATE_GPR_INDEX, count + 1U);
}

void bsp_boot_attempt_reset(void)
{
    SRC_SetGeneralPurposeRegister(BSP_BOOT_STATE_SRC_BASE, BSP_BOOT_STATE_GPR_INDEX, 0U);
}

void bsp_boot_health_mark(void)
{
    bsp_boot_attempt_reset();
}

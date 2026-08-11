/*
 * bsp_reset — доступ к SRC->SRSR (источник последнего сброса).
 *
 * Только железо: чтение, отображение бит чипа на флаги BSP и очистка.
 * Разбор маски в текст — reset_decode.c (чистый, host-тестируемый).
 */

#include "bsp/reset.h"

#include "fsl_src.h"

/**
 * @brief Отображение бит SRSR (MIMXRT1052) на флаги BSP.
 *
 * Таблицей, а не совпадением числовых значений: раскладка регистра — факт
 * конкретного чипа, флаги BSP — контракт модуля. На другом чипе меняется
 * только эта таблица.
 */
typedef struct
{
    uint32_t srsr_mask;
    uint32_t bsp_flag;
} reset_bit_map_t;

static const reset_bit_map_t K_BIT_MAP[] = {
    { SRC_SRSR_IPP_RESET_B_MASK, BSP_RESET_SRC_POWER_UP },
    { SRC_SRSR_IPP_USER_RESET_B_MASK, BSP_RESET_SRC_USER_PIN },
    { SRC_SRSR_WDOG_RST_B_MASK, BSP_RESET_SRC_WDOG },
    { SRC_SRSR_WDOG3_RST_B_MASK, BSP_RESET_SRC_WDOG3 },
    { SRC_SRSR_LOCKUP_SYSRESETREQ_MASK, BSP_RESET_SRC_SW_OR_LOCKUP },
    { SRC_SRSR_TEMPSENSE_RST_B_MASK, BSP_RESET_SRC_OVERHEAT },
    { SRC_SRSR_JTAG_RST_B_MASK, BSP_RESET_SRC_JTAG },
    { SRC_SRSR_JTAG_SW_RST_MASK, BSP_RESET_SRC_JTAG_SW },
    { SRC_SRSR_CSU_RESET_B_MASK, BSP_RESET_SRC_CSU },
};

#define BIT_MAP_COUNT (sizeof(K_BIT_MAP) / sizeof(K_BIT_MAP[0]))

/* Все известные биты — то, что снимаем при очистке. */
#define SRSR_KNOWN_BITS                                                                            \
    (SRC_SRSR_W1C_BITS_MASK | SRC_SRSR_TEMPSENSE_RST_B_MASK) /* см. очистку ниже */

static uint32_t g_s_raw     = 0U;
static uint32_t g_s_sources = BSP_RESET_SRC_NONE;
static bool g_s_captured    = false;

void bsp_reset_capture(void)
{
    if (g_s_captured)
    {
        return; /* латч уже снят; регистр очищен, повторное чтение дало бы пусто */
    }
    g_s_captured = true;

    g_s_raw = SRC_GetResetStatusFlags(SRC);

    for (uint32_t i = 0U; i < (uint32_t) BIT_MAP_COUNT; i++)
    {
        if ((g_s_raw & K_BIT_MAP[i].srsr_mask) != 0U)
        {
            g_s_sources |= K_BIT_MAP[i].bsp_flag;
        }
    }

    /* Очистка — ОБЯЗАТЕЛЬНА: биты залипают до явного снятия, иначе со второй
     * перезагрузки они накапливаются и отчёт врёт.
     *
     * Через SDK, а НЕ прямой записью в регистр: у RT1052 биты снимаются
     * ПО-РАЗНОМУ — TEMPSENSE_RST_B записью нуля, все прочие
     * (SRC_SRSR_W1C_BITS_MASK) записью единицы. Рукописное `SRC->SRSR = mask`
     * на этом и ломается. */
    SRC_ClearResetStatusFlags(SRC, SRSR_KNOWN_BITS);
}

uint32_t bsp_reset_sources(void)
{
    return g_s_sources;
}

uint32_t bsp_reset_raw(void)
{
    return g_s_raw;
}

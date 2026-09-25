/**
 * @file  qspi_check.c
 * @brief Платформо-независимая часть hil_flexspi_stress (см. qspi_check.h).
 */

#include "qspi_check.h"

#include <stdbool.h>

#define PLL3_KHZ       480000U
#define PFD_MUL        18U
#define PFD_FRAC_MIN   12U /* RM: CCM_ANALOG_PFD_480 */
#define PFD_FRAC_MAX   35U
#define NIBBLES_IN_U32 8U

/*
 * Ступени частоты. 133 — как FCB (serialClkFreq=7), 60 — предел даташита для
 * RXCLKSRC=0 (IMXRT1050IEC табл. 37), 30 — безопасная частота для записи.
 * Остальные — лесенка для поиска края внутренней петли.
 */
static const qspi_clk_plan_t K_PLANS[] = {
    {30U, 35U, 8U},  /* 246,86 / 8 = 30,86 */
    {60U, 18U, 8U},  /* 480,00 / 8 = 60,00 */
    {80U, 18U, 6U},  /* 480,00 / 6 = 80,00 */
    {99U, 29U, 3U},  /* 297,93 / 3 = 99,31 */
    {120U, 24U, 3U}, /* 360,00 / 3 = 120,00 (как пример SDK) */
    {133U, 13U, 5U}, /* 664,62 / 5 = 132,92 */
};

void qspi_result_clear(qspi_result_t *p_res)
{
    p_res->errors       = 0U;
    p_res->first_offset = 0U;
    p_res->expected     = 0U;
    p_res->actual       = 0U;
    p_res->diff_or      = 0U;
}

uint32_t qspi_prng_seed(uint32_t seed)
{
    return (seed == 0U) ? 1U : seed;
}

uint32_t qspi_prng_next(uint32_t *p_state)
{
    uint32_t x = *p_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *p_state = x;
    return x;
}

void qspi_prng_fill(uint32_t *p_dst, size_t words, uint32_t *p_state)
{
    for (size_t i = 0U; i < words; i++)
    {
        p_dst[i] = qspi_prng_next(p_state);
    }
}

void qspi_verify(const volatile uint32_t *p_words, size_t words, uint32_t *p_state, uint32_t offset,
                 qspi_result_t *p_res)
{
    for (size_t i = 0U; i < words; i++)
    {
        const uint32_t EXP = qspi_prng_next(p_state);
        const uint32_t ACT = p_words[i];
        if (ACT != EXP)
        {
            if (p_res->errors == 0U)
            {
                p_res->first_offset = offset + (uint32_t) (i * sizeof(uint32_t));
                p_res->expected     = EXP;
                p_res->actual       = ACT;
            }
            if (p_res->errors != UINT32_MAX)
            {
                p_res->errors++;
            }
            p_res->diff_or |= EXP ^ ACT;
        }
    }
}

uint8_t qspi_io_mask(uint32_t diff_or)
{
    uint32_t mask = 0U;
    for (uint32_t n = 0U; n < NIBBLES_IN_U32; n++)
    {
        mask |= (diff_or >> (4U * n)) & 0xFU;
    }
    return (uint8_t) mask;
}

const qspi_clk_plan_t *qspi_clk_plan_find(uint32_t mhz)
{
    for (size_t i = 0U; i < (sizeof(K_PLANS) / sizeof(K_PLANS[0])); i++)
    {
        const qspi_clk_plan_t *const P_PLAN = &K_PLANS[i];
        const bool VALID = (P_PLAN->pfd0_frac >= PFD_FRAC_MIN) && (P_PLAN->pfd0_frac <= PFD_FRAC_MAX);
        if ((P_PLAN->mhz == mhz) && VALID)
        {
            return P_PLAN;
        }
    }
    return NULL;
}

uint32_t qspi_clk_khz(const qspi_clk_plan_t *p_plan)
{
    return (PLL3_KHZ * PFD_MUL / p_plan->pfd0_frac) / p_plan->podf;
}

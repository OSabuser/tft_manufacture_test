/*
 * test_qspi_check.c — host-тест платформо-независимой части hil_flexspi_stress.
 *
 * Категория A: PRNG-паттерн (запись и сверка должны давать одну и ту же
 * последовательность по частям), учёт ошибок, свёртка расхождений в линии
 * IO0..3 и план тактирования FlexSPI (FRAC в пределах RM, частоты не выше
 * заявленных).
 */

#include "unity.h"

#include "qspi_check.h"

#include <string.h>

#define WORDS 1024U

static uint32_t s_buf[WORDS];
static qspi_result_t s_res;

void setUp(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    qspi_result_clear(&s_res);
}

void tearDown(void) {}

static void fill(uint32_t seed)
{
    uint32_t state = qspi_prng_seed(seed);
    qspi_prng_fill(s_buf, WORDS, &state);
}

/* ── PRNG и сверка ─────────────────────────────────────────────────────── */

void test_verify_clean_pattern_has_no_errors(void)
{
    fill(7U);
    uint32_t state = qspi_prng_seed(7U);
    qspi_verify(s_buf, WORDS, &state, 0U, &s_res);
    TEST_ASSERT_EQUAL_UINT32(0U, s_res.errors);
    TEST_ASSERT_EQUAL_UINT32(0U, s_res.diff_or);
}

void test_verify_in_chunks_equals_whole(void)
{
    fill(3U);
    uint32_t state = qspi_prng_seed(3U);
    for (uint32_t w = 0U; w < WORDS; w += 64U)
    {
        qspi_verify(&s_buf[w], 64U, &state, w * 4U, &s_res);
    }
    TEST_ASSERT_EQUAL_UINT32(0U, s_res.errors);
}

void test_seed_zero_is_not_stuck(void)
{
    fill(0U);
    TEST_ASSERT_NOT_EQUAL(0U, s_buf[0]);
    TEST_ASSERT_NOT_EQUAL(s_buf[0], s_buf[1]);
}

void test_wrong_seed_fails(void)
{
    fill(1U);
    uint32_t state = qspi_prng_seed(2U);
    qspi_verify(s_buf, WORDS, &state, 0U, &s_res);
    TEST_ASSERT_GREATER_THAN_UINT32(WORDS / 2U, s_res.errors);
}

void test_first_error_offset_and_values(void)
{
    fill(5U);
    const uint32_t GOOD = s_buf[100];
    s_buf[100] ^= 0x00000010U;
    s_buf[200] ^= 0x80000000U;
    uint32_t state = qspi_prng_seed(5U);
    qspi_verify(s_buf, WORDS, &state, 0x1000U, &s_res);
    TEST_ASSERT_EQUAL_UINT32(2U, s_res.errors);
    TEST_ASSERT_EQUAL_HEX32(0x1000U + (100U * 4U), s_res.first_offset);
    TEST_ASSERT_EQUAL_HEX32(GOOD, s_res.expected);
    TEST_ASSERT_EQUAL_HEX32(GOOD ^ 0x00000010U, s_res.actual);
    TEST_ASSERT_EQUAL_HEX32(0x80000010U, s_res.diff_or);
}

/* ── Линии IO ──────────────────────────────────────────────────────────── */

void test_io_mask_single_line(void)
{
    /* IO2 — бит 2 каждого полубайта */
    TEST_ASSERT_EQUAL_HEX8(0x4U, qspi_io_mask(0x44444444U));
    TEST_ASSERT_EQUAL_HEX8(0x4U, qspi_io_mask(0x00400000U));
}

void test_io_mask_folds_all_nibbles(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x0U, qspi_io_mask(0U));
    TEST_ASSERT_EQUAL_HEX8(0xFU, qspi_io_mask(0x80402010U) | qspi_io_mask(0x01020408U));
    TEST_ASSERT_EQUAL_HEX8(0x9U, qspi_io_mask(0x10000008U));
}

/* ── План тактирования ─────────────────────────────────────────────────── */

void test_clock_plans_match_nominal(void)
{
    static const uint32_t K_MHZ[] = {30U, 60U, 80U, 99U, 120U, 133U};
    for (size_t i = 0U; i < (sizeof(K_MHZ) / sizeof(K_MHZ[0])); i++)
    {
        const qspi_clk_plan_t *const P = qspi_clk_plan_find(K_MHZ[i]);
        TEST_ASSERT_NOT_NULL(P);
        TEST_ASSERT_TRUE(P->pfd0_frac >= 12U && P->pfd0_frac <= 35U);
        TEST_ASSERT_TRUE(P->podf >= 1U && P->podf <= 8U);
        const uint32_t KHZ = qspi_clk_khz(P);
        /* не выше номинала +1 МГц и не ниже −1,5 МГц */
        TEST_ASSERT_TRUE(KHZ <= (K_MHZ[i] * 1000U) + 1000U);
        TEST_ASSERT_TRUE(KHZ + 1500U >= K_MHZ[i] * 1000U);
    }
}

void test_clock_133_is_fcb_rate_and_not_above_flash_limit(void)
{
    const qspi_clk_plan_t *const P = qspi_clk_plan_find(133U);
    TEST_ASSERT_NOT_NULL(P);
    TEST_ASSERT_EQUAL_UINT32(132923U, qspi_clk_khz(P)); /* ≤ 133 МГц W25Q128JV */
}

void test_unknown_clock_is_rejected(void)
{
    TEST_ASSERT_NULL(qspi_clk_plan_find(0U));
    TEST_ASSERT_NULL(qspi_clk_plan_find(166U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_verify_clean_pattern_has_no_errors);
    RUN_TEST(test_verify_in_chunks_equals_whole);
    RUN_TEST(test_seed_zero_is_not_stuck);
    RUN_TEST(test_wrong_seed_fails);
    RUN_TEST(test_first_error_offset_and_values);
    RUN_TEST(test_io_mask_single_line);
    RUN_TEST(test_io_mask_folds_all_nibbles);
    RUN_TEST(test_clock_plans_match_nominal);
    RUN_TEST(test_clock_133_is_fcb_rate_and_not_above_flash_limit);
    RUN_TEST(test_unknown_clock_is_rejected);
    return UNITY_END();
}

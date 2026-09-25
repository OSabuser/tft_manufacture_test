/*
 * test_mem_tests.c — host-тест алгоритмов hil_sdram_stress (mem_tests.c).
 *
 * Категория A. Память — обычный буфер. Неисправности имитируются колбэками:
 * порча слова между заполнением и проверкой, «залипший» бит (progress),
 * «замкнутая адресная линия» (sync копирует ячейку в её «тень»).
 * MEM_PROGRESS_WORDS переопределён на 16 (CMake), чтобы колбэк звался часто.
 */

#include "unity.h"

#include "mem_tests.h"

#include <string.h>

#define WORDS 4096U

static uint32_t s_buf[WORDS];
static mem_result_t s_res;
static uint32_t s_progress_calls;
static uint32_t s_sync_calls;

void setUp(void)
{
    memset(s_buf, 0, sizeof(s_buf));
    mem_result_clear(&s_res);
    s_progress_calls = 0U;
    s_sync_calls     = 0U;
}

void tearDown(void) {}

/* ── Исправная память ──────────────────────────────────────────────────── */

void test_clean_memory_passes_all_tests(void)
{
    mem_test_databus(&s_buf[0], &s_res);
    mem_test_addrbus(s_buf, WORDS, NULL, &s_res);
    mem_test_march_c(s_buf, WORDS, 0x00000000UL, NULL, &s_res);
    mem_test_march_c(s_buf, WORDS, 0x55555555UL, NULL, &s_res);
    mem_prng_fill(s_buf, WORDS, 1234U, NULL);
    mem_prng_verify(s_buf, WORDS, 1234U, NULL, &s_res);

    TEST_ASSERT_EQUAL_UINT32(0U, s_res.errors);
    TEST_ASSERT_EQUAL_HEX32(0U, s_res.diff_or);
}

void test_march_leaves_background_in_memory(void)
{
    mem_test_march_c(s_buf, WORDS, 0xA5A5A5A5UL, NULL, &s_res);
    TEST_ASSERT_EACH_EQUAL_HEX32(0xA5A5A5A5UL, s_buf, WORDS);
}

/* ── PRNG ──────────────────────────────────────────────────────────────── */

void test_prng_detects_single_corrupted_word(void)
{
    mem_prng_fill(s_buf, WORDS, 42U, NULL);
    const uint32_t GOOD = s_buf[100];
    s_buf[100] ^= 0x00010004UL;

    mem_prng_verify(s_buf, WORDS, 42U, NULL, &s_res);

    TEST_ASSERT_EQUAL_UINT32(1U, s_res.errors);
    TEST_ASSERT_EQUAL_UINT32(400U, s_res.first_offset);
    TEST_ASSERT_EQUAL_HEX32(GOOD, s_res.expected);
    TEST_ASSERT_EQUAL_HEX32(GOOD ^ 0x00010004UL, s_res.actual);
    TEST_ASSERT_EQUAL_HEX32(0x00010004UL, s_res.diff_or);
    TEST_ASSERT_EQUAL_HEX16(0x0005U, mem_dq_mask(&s_res)); /* DQ0 (верх. полуслово) + DQ2 */
}

void test_prng_seed_zero_is_not_degenerate(void)
{
    mem_prng_fill(s_buf, 16U, 0U, NULL);
    TEST_ASSERT_NOT_EQUAL(0U, s_buf[0]);
    TEST_ASSERT_NOT_EQUAL(s_buf[0], s_buf[1]);
}

void test_prng_different_seed_fails(void)
{
    mem_prng_fill(s_buf, WORDS, 1U, NULL);
    mem_prng_verify(s_buf, WORDS, 2U, NULL, &s_res);
    TEST_ASSERT_EQUAL_UINT32(WORDS, s_res.errors);
}

/* ── Неисправности через колбэки ───────────────────────────────────────── */

/* «Залипший в 1» бит 4 в слове 777 — навязывается при каждом progress. */
static void stuck_bit_progress(void)
{
    s_progress_calls++;
    s_buf[777] |= 0x10U;
}

void test_march_detects_stuck_bit(void)
{
    const mem_hooks_t HOOKS = {.sync = NULL, .progress = stuck_bit_progress};
    mem_test_march_c(s_buf, WORDS, 0x00000000UL, &HOOKS, &s_res);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_progress_calls);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_res.errors);
    TEST_ASSERT_EQUAL_HEX32(0x10U, s_res.diff_or);
}

/* «Замкнутая» адресная линия: запись в слово 8 проявляется и в слове 16. */
static void alias_sync(void)
{
    s_sync_calls++;
    s_buf[16] = s_buf[8];
}

void test_addrbus_detects_aliasing(void)
{
    const mem_hooks_t HOOKS = {.sync = alias_sync, .progress = NULL};
    mem_test_addrbus(s_buf, WORDS, &HOOKS, &s_res);

    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_sync_calls);
    TEST_ASSERT_GREATER_THAN_UINT32(0U, s_res.errors);
}

void test_addrbus_touches_only_power_of_two_offsets(void)
{
    for (uint32_t i = 0U; i < WORDS; i++)
    {
        s_buf[i] = 0xDEADBEEFUL;
    }
    mem_test_addrbus(s_buf, WORDS, NULL, &s_res);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFUL, s_buf[3]);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFUL, s_buf[WORDS - 1U]);
    TEST_ASSERT_EQUAL_HEX32(0xAAAAAAAAUL, s_buf[1024]);
}

void test_sync_called_before_verification(void)
{
    const mem_hooks_t HOOKS = {.sync = alias_sync, .progress = NULL};
    mem_prng_fill(s_buf, WORDS, 7U, &HOOKS);
    const uint32_t AFTER_FILL = s_sync_calls;
    mem_prng_verify(s_buf, WORDS, 7U, &HOOKS, &s_res);
    TEST_ASSERT_EQUAL_UINT32(1U, AFTER_FILL);
    TEST_ASSERT_EQUAL_UINT32(2U, s_sync_calls);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clean_memory_passes_all_tests);
    RUN_TEST(test_march_leaves_background_in_memory);
    RUN_TEST(test_prng_detects_single_corrupted_word);
    RUN_TEST(test_prng_seed_zero_is_not_degenerate);
    RUN_TEST(test_prng_different_seed_fails);
    RUN_TEST(test_march_detects_stuck_bit);
    RUN_TEST(test_addrbus_detects_aliasing);
    RUN_TEST(test_addrbus_touches_only_power_of_two_offsets);
    RUN_TEST(test_sync_called_before_verification);
    return UNITY_END();
}

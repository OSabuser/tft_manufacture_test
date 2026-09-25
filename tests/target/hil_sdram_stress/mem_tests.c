/**
 * @file  mem_tests.c
 * @brief Алгоритмы проверки внешней памяти — см. mem_tests.h.
 */

#include "mem_tests.h"

#include <stdbool.h>

#define WORD_BYTES 4U

/* ── Служебное ─────────────────────────────────────────────────────────── */

void mem_result_clear(mem_result_t *p_res)
{
    p_res->errors       = 0U;
    p_res->first_offset = 0U;
    p_res->expected     = 0U;
    p_res->actual       = 0U;
    p_res->diff_or      = 0U;
}

uint16_t mem_dq_mask(const mem_result_t *p_res)
{
    return (uint16_t) ((p_res->diff_or & 0xFFFFU) | (p_res->diff_or >> 16));
}

static void record(mem_result_t *p_res, size_t word_index, uint32_t expected, uint32_t actual)
{
    if (p_res->errors == 0U)
    {
        p_res->first_offset = (uint32_t) (word_index * WORD_BYTES);
        p_res->expected     = expected;
        p_res->actual       = actual;
    }
    if (p_res->errors != UINT32_MAX)
    {
        p_res->errors++;
    }
    p_res->diff_or |= expected ^ actual;
}

static void check(volatile uint32_t *p_base, size_t i, uint32_t expected, mem_result_t *p_res)
{
    const uint32_t ACTUAL = p_base[i];
    if (ACTUAL != expected)
    {
        record(p_res, i, expected, ACTUAL);
    }
}

static void hook_sync(const mem_hooks_t *p_hooks)
{
    if ((p_hooks != NULL) && (p_hooks->sync != NULL))
    {
        p_hooks->sync();
    }
}

static void hook_progress(const mem_hooks_t *p_hooks, size_t i)
{
    if ((p_hooks != NULL) && (p_hooks->progress != NULL) &&
        ((i & (MEM_PROGRESS_WORDS - 1U)) == 0U))
    {
        p_hooks->progress();
    }
}

/* ── Шина данных ───────────────────────────────────────────────────────── */

void mem_test_databus(volatile uint32_t *p_word, mem_result_t *p_res)
{
    for (uint32_t bit = 0U; bit < 32U; bit++)
    {
        const uint32_t ONES = 1UL << bit;
        *p_word             = ONES;
        check(p_word, 0U, ONES, p_res);
        *p_word = ~ONES;
        check(p_word, 0U, ~ONES, p_res);
    }
}

/* ── Шина адреса ───────────────────────────────────────────────────────── */

#define ADDR_PATTERN      0xAAAAAAAAUL
#define ADDR_ANTI_PATTERN 0x55555555UL

void mem_test_addrbus(volatile uint32_t *p_base, size_t words, const mem_hooks_t *p_hooks,
                      mem_result_t *p_res)
{
    /* Все ячейки-степени двойки + нулевая — паттерн. */
    p_base[0] = ADDR_PATTERN;
    for (size_t off = 1U; off < words; off <<= 1U)
    {
        p_base[off] = ADDR_PATTERN;
    }

    /* Залипание в 1: пишем анти-паттерн в 0 — остальные не должны измениться. */
    p_base[0] = ADDR_ANTI_PATTERN;
    hook_sync(p_hooks);
    for (size_t off = 1U; off < words; off <<= 1U)
    {
        check(p_base, off, ADDR_PATTERN, p_res);
    }
    p_base[0] = ADDR_PATTERN;

    /* Залипание в 0 и замыкания: по очереди анти-паттерн в каждую степень двойки. */
    for (size_t test = 1U; test < words; test <<= 1U)
    {
        p_base[test] = ADDR_ANTI_PATTERN;
        hook_sync(p_hooks);
        check(p_base, 0U, ADDR_PATTERN, p_res);
        for (size_t off = 1U; off < words; off <<= 1U)
        {
            check(p_base, off, (off == test) ? ADDR_ANTI_PATTERN : ADDR_PATTERN, p_res);
        }
        p_base[test] = ADDR_PATTERN;
    }
}

/* ── March C− ──────────────────────────────────────────────────────────── */

static void march_write_up(volatile uint32_t *p_base, size_t words, uint32_t value,
                           const mem_hooks_t *p_hooks)
{
    for (size_t i = 0U; i < words; i++)
    {
        p_base[i] = value;
        hook_progress(p_hooks, i);
    }
}

static void march_rw_up(volatile uint32_t *p_base, size_t words, uint32_t expect, uint32_t write,
                        const mem_hooks_t *p_hooks, mem_result_t *p_res)
{
    for (size_t i = 0U; i < words; i++)
    {
        check(p_base, i, expect, p_res);
        p_base[i] = write;
        hook_progress(p_hooks, i);
    }
}

static void march_rw_down(volatile uint32_t *p_base, size_t words, uint32_t expect,
                          uint32_t write, const mem_hooks_t *p_hooks, mem_result_t *p_res)
{
    for (size_t n = words; n > 0U; n--)
    {
        const size_t I = n - 1U;
        check(p_base, I, expect, p_res);
        p_base[I] = write;
        hook_progress(p_hooks, I);
    }
}

void mem_test_march_c(volatile uint32_t *p_base, size_t words, uint32_t background,
                      const mem_hooks_t *p_hooks, mem_result_t *p_res)
{
    const uint32_t ZERO = background;
    const uint32_t ONE  = ~background;

    march_write_up(p_base, words, ZERO, p_hooks);
    hook_sync(p_hooks);
    march_rw_up(p_base, words, ZERO, ONE, p_hooks, p_res);
    hook_sync(p_hooks);
    march_rw_up(p_base, words, ONE, ZERO, p_hooks, p_res);
    hook_sync(p_hooks);
    march_rw_down(p_base, words, ZERO, ONE, p_hooks, p_res);
    hook_sync(p_hooks);
    march_rw_down(p_base, words, ONE, ZERO, p_hooks, p_res);
    hook_sync(p_hooks);
    for (size_t i = 0U; i < words; i++)
    {
        check(p_base, i, ZERO, p_res);
        hook_progress(p_hooks, i);
    }
}

/* ── PRNG ──────────────────────────────────────────────────────────────── */

static uint32_t xorshift32(uint32_t state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static uint32_t seed_state(uint32_t seed)
{
    return (seed != 0U) ? seed : 0x2545F491UL; /* xorshift не выходит из нуля */
}

void mem_prng_fill(volatile uint32_t *p_base, size_t words, uint32_t seed,
                   const mem_hooks_t *p_hooks)
{
    uint32_t state = seed_state(seed);
    for (size_t i = 0U; i < words; i++)
    {
        state     = xorshift32(state);
        p_base[i] = state;
        hook_progress(p_hooks, i);
    }
    hook_sync(p_hooks);
}

void mem_prng_verify(volatile uint32_t *p_base, size_t words, uint32_t seed,
                     const mem_hooks_t *p_hooks, mem_result_t *p_res)
{
    uint32_t state = seed_state(seed);
    hook_sync(p_hooks);
    for (size_t i = 0U; i < words; i++)
    {
        state = xorshift32(state);
        check(p_base, i, state, p_res);
        hook_progress(p_hooks, i);
    }
}

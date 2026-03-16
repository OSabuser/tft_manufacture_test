/**
 * @file  test_timeout_pattern.c
 * @brief Тесты паттерна таймаута (bsp_tick_get_ms() - start) < timeout.
 *
 * Паттерн используется в bsp_uart_read(), bsp_delay() и других
 * BSP-модулях. Тестируем математику изолированно — без железа и моков.
 *
 * Ключевое свойство: беззнаковая арифметика uint32_t корректна
 * при wraparound счётчика (переполнение через UINT32_MAX).
 */

#include "unity.h"

#include <stdint.h>

void setUp(void)
{
}
void tearDown(void)
{
}

/* ── Вспомогательный макрос — сам паттерн ───────────────────────────── */

/**
 * Возвращает 1 если таймаут истёк.
 * now и start — uint32_t (как bsp_tick_get_ms()).
 */
#define TIMEOUT_EXPIRED(now, start, timeout_ms)                                                    \
    (((uint32_t) (now) - (uint32_t) (start)) >= (uint32_t) (timeout_ms))

/* ═══════════════════════════════════════════════════════════════════════
 * 1. Нормальные случаи (без wraparound)
 * ═══════════════════════════════════════════════════════════════════════ */

void test_not_expired_elapsed_less_than_timeout(void)
{
    uint32_t start   = 1000U;
    uint32_t now     = 1099U;
    uint32_t timeout = 100U;

    TEST_ASSERT_FALSE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_expired_elapsed_equals_timeout(void)
{
    /* Граничное условие: elapsed == timeout → истёк. */
    uint32_t start   = 1000U;
    uint32_t now     = 1100U;
    uint32_t timeout = 100U;

    TEST_ASSERT_TRUE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_expired_elapsed_greater_than_timeout(void)
{
    uint32_t start   = 1000U;
    uint32_t now     = 1200U;
    uint32_t timeout = 100U;

    TEST_ASSERT_TRUE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_not_expired_at_zero_elapsed(void)
{
    /* now == start: ничего не прошло. */
    uint32_t start   = 5000U;
    uint32_t now     = 5000U;
    uint32_t timeout = 1U;

    TEST_ASSERT_FALSE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_zero_timeout_always_expired(void)
{
    /*
     * timeout == 0: 0 >= 0 всегда true.
     * Это ожидаемое поведение — вызывающий код трактует 0
     * как «не ждать вообще».
     */
    uint32_t start   = 1000U;
    uint32_t now     = 1000U;
    uint32_t timeout = 0U;

    TEST_ASSERT_TRUE(TIMEOUT_EXPIRED(now, start, timeout));
}

/* ═══════════════════════════════════════════════════════════════════════
 * 2. Wraparound — счётчик переполнился через UINT32_MAX
 * ═══════════════════════════════════════════════════════════════════════ */

void test_wraparound_not_expired(void)
{
    /*
     * start близко к UINT32_MAX, now уже за нулём.
     * elapsed = (50 - (UINT32_MAX - 49)) = 100 в беззнаковой арифметике.
     * timeout = 150 → ещё не истёк.
     */
    uint32_t start   = UINT32_MAX - 49U; /* 4294967246 */
    uint32_t now     = 50U;
    uint32_t timeout = 150U;

    /* elapsed = now - start = 50 - 4294967246 = 100 (mod 2^32) */
    TEST_ASSERT_EQUAL_UINT32(100U, now - start);
    TEST_ASSERT_FALSE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_wraparound_exactly_at_timeout(void)
{
    uint32_t start   = UINT32_MAX - 49U;
    uint32_t now     = 50U;
    uint32_t timeout = 100U; /* elapsed == timeout → истёк */

    TEST_ASSERT_TRUE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_wraparound_expired(void)
{
    uint32_t start   = UINT32_MAX - 49U;
    uint32_t now     = 100U;
    uint32_t timeout = 100U;

    /* elapsed = 150 >= 100 → истёк */
    TEST_ASSERT_TRUE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_wraparound_start_at_max(void)
{
    /* start = UINT32_MAX, now = 0: elapsed = 1 */
    uint32_t start   = UINT32_MAX;
    uint32_t now     = 0U;
    uint32_t timeout = 1U;

    TEST_ASSERT_EQUAL_UINT32(1U, now - start);
    TEST_ASSERT_TRUE(TIMEOUT_EXPIRED(now, start, timeout));
}

void test_wraparound_start_at_max_not_expired(void)
{
    /* start = UINT32_MAX, now = 0: elapsed = 1, timeout = 2 → не истёк */
    uint32_t start   = UINT32_MAX;
    uint32_t now     = 0U;
    uint32_t timeout = 2U;

    TEST_ASSERT_FALSE(TIMEOUT_EXPIRED(now, start, timeout));
}

/* ═══════════════════════════════════════════════════════════════════════
 * 3. Почему альтернативный паттерн НЕВЕРЕН
 *
 * Документируем антипаттерн: (now >= start + timeout).
 * При wraparound start + timeout переполняется и сравнение ломается.
 * Эти тесты показывают конкретные случаи поломки.
 * ═══════════════════════════════════════════════════════════════════════ */

void test_antipattern_breaks_at_wraparound(void)
{
    /*
     * Антипаттерн: now >= (start + timeout)
     * start = UINT32_MAX - 49, timeout = 100
     * start + timeout = UINT32_MAX - 49 + 100 = 50 (переполнение!)
     *
     * now = 80: реальный elapsed = 130 >= 100 → должно быть expired.
     * Но антипаттерн: 80 >= 50 → true (случайно верно здесь)
     *
     * now = 30: реальный elapsed = 80 < 100 → не истёк.
     * Антипаттерн: 30 >= 50 → false (верно, но по случайности)
     *
     * now = 40: реальный elapsed = 90 < 100 → не истёк.
     * Антипаттерн: 40 >= 50 → false (верно)
     *
     * now = 60: реальный elapsed = 110 >= 100 → истёк.
     * Антипаттерн: 60 >= 50 → true (верно)
     *
     * Сложный случай — start близко к MAX, timeout большой:
     * start = UINT32_MAX - 10, timeout = UINT32_MAX - 100
     * start + timeout переполняется в очень маленькое число →
     * антипаттерн скажет "истёк" почти сразу.
     */

    uint32_t start   = UINT32_MAX - 10U;
    uint32_t timeout = 1000U;
    uint32_t now     = start + 5U; /* elapsed = 5, далеко до timeout */

    /* Правильный паттерн: не истёк (5 < 1000) */
    TEST_ASSERT_FALSE(TIMEOUT_EXPIRED(now, start, timeout));

    /* Антипаттерн: start + timeout переполнился → (start+timeout) маленькое,
     * now > (start+timeout) → антипаттерн скажет "истёк" — НЕВЕРНО. */
    uint32_t wrong_deadline = start + timeout; /* wraparound! */
    int antipattern_result  = (now >= wrong_deadline);

    /* Демонстрируем что антипаттерн даёт неверный результат: */
    TEST_ASSERT_TRUE(antipattern_result); /* антипаттерн говорит "истёк" */
    TEST_ASSERT_FALSE(TIMEOUT_EXPIRED(now, start, timeout)); /* правильно: нет */
}

/* ── Runner ──────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    /* Нормальные случаи */
    RUN_TEST(test_not_expired_elapsed_less_than_timeout);
    RUN_TEST(test_expired_elapsed_equals_timeout);
    RUN_TEST(test_expired_elapsed_greater_than_timeout);
    RUN_TEST(test_not_expired_at_zero_elapsed);
    RUN_TEST(test_zero_timeout_always_expired);

    /* Wraparound */
    RUN_TEST(test_wraparound_not_expired);
    RUN_TEST(test_wraparound_exactly_at_timeout);
    RUN_TEST(test_wraparound_expired);
    RUN_TEST(test_wraparound_start_at_max);
    RUN_TEST(test_wraparound_start_at_max_not_expired);

    /* Антипаттерн */
    RUN_TEST(test_antipattern_breaks_at_wraparound);

    return UNITY_END();
}
/**
 * @file  test_prio_queue.c
 * @brief Host unit-тесты для prio_queue (Категория A — без NXP SDK).
 *
 * Фреймворк: Unity.
 * Запуск:    ctest --preset host-debug-test -R test_prio_queue -V
 *
 * Проверяемый инвариант:
 *   - peek()/remove_at(0) всегда отдаёт элемент с наивысшим приоритетом.
 *   - При равных приоритетах — FIFO (первым вошёл, первым вышел).
 */

#include "prio_queue/prio_queue.h"
#include "unity.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/* ── Тестовые типы ─────────────────────────────────────────────────────── */

typedef struct
{
    int priority;
    const char *name;
} task_t;

/* cmp: меньший priority → ближе к голове (выше приоритет) */
static int task_cmp(const void *p_a, const void *p_b)
{
    int pa = ((const task_t *) p_a)->priority;
    int pb = ((const task_t *) p_b)->priority;
    return (pa < pb) ? -1 : (pa > pb) ? 1 : 0;
}

/* ── Общие фикстуры ────────────────────────────────────────────────────── */

#define CAPACITY 8U

static task_t s_storage[CAPACITY];
static prio_queue_t s_q;

void setUp(void)
{
    memset(s_storage, 0, sizeof(s_storage));
    prio_queue_init(&s_q, s_storage, CAPACITY, sizeof(task_t), task_cmp);
}

void tearDown(void)
{
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: init
 * ══════════════════════════════════════════════════════════════════════════ */

void test_init_size_is_zero(void)
{
    TEST_ASSERT_EQUAL_UINT8(0U, prio_queue_size(&s_q));
}

void test_init_peek_returns_null(void)
{
    TEST_ASSERT_NULL(prio_queue_peek(&s_q));
}

void test_init_at_returns_null_when_empty(void)
{
    TEST_ASSERT_NULL(prio_queue_at(&s_q, 0U));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: insert_order — правильная сортировка
 * ══════════════════════════════════════════════════════════════════════════ */

void test_insert_single_element(void)
{
    task_t t        = { .priority = 5, .name = "t5" };
    pq_status_t ret = prio_queue_insert(&s_q, &t);

    TEST_ASSERT_EQUAL(PQ_OK, ret);
    TEST_ASSERT_EQUAL_UINT8(1U, prio_queue_size(&s_q));
}

void test_insert_two_ascending_order_peek_is_higher(void)
{
    /* Вставляем менее приоритетный первым */
    task_t low  = { .priority = 10, .name = "low" };
    task_t high = { .priority = 1, .name = "high" };

    prio_queue_insert(&s_q, &low);
    prio_queue_insert(&s_q, &high);

    const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
    TEST_ASSERT_NOT_NULL(p_top);
    TEST_ASSERT_EQUAL_STRING("high", p_top->name);
}

void test_insert_descending_input_peek_is_still_highest(void)
{
    task_t a = { .priority = 3, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };
    task_t c = { .priority = 1, .name = "c" };

    /* Вставляем в убывающем порядке приоритета */
    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);
    prio_queue_insert(&s_q, &c);

    const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
    TEST_ASSERT_NOT_NULL(p_top);
    TEST_ASSERT_EQUAL_STRING("c", p_top->name);
}

void test_insert_ascending_input_order_preserved(void)
{
    task_t a = { .priority = 1, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };
    task_t c = { .priority = 3, .name = "c" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);
    prio_queue_insert(&s_q, &c);

    /* Проверяем полный порядок */
    const task_t *p0 = (const task_t *) prio_queue_at(&s_q, 0U);
    const task_t *p1 = (const task_t *) prio_queue_at(&s_q, 1U);
    const task_t *p2 = (const task_t *) prio_queue_at(&s_q, 2U);

    TEST_ASSERT_EQUAL_STRING("a", p0->name);
    TEST_ASSERT_EQUAL_STRING("b", p1->name);
    TEST_ASSERT_EQUAL_STRING("c", p2->name);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: fifo — FIFO при равных приоритетах
 * ══════════════════════════════════════════════════════════════════════════ */

void test_fifo_two_equal_priority_first_in_first_out(void)
{
    task_t first  = { .priority = 2, .name = "first" };
    task_t second = { .priority = 2, .name = "second" };

    prio_queue_insert(&s_q, &first);
    prio_queue_insert(&s_q, &second);

    const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
    TEST_ASSERT_NOT_NULL(p_top);
    TEST_ASSERT_EQUAL_STRING("first", p_top->name);
}

void test_fifo_three_equal_priority_order_preserved(void)
{
    task_t a = { .priority = 5, .name = "a" };
    task_t b = { .priority = 5, .name = "b" };
    task_t c = { .priority = 5, .name = "c" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);
    prio_queue_insert(&s_q, &c);

    const task_t *p0 = (const task_t *) prio_queue_at(&s_q, 0U);
    const task_t *p1 = (const task_t *) prio_queue_at(&s_q, 1U);
    const task_t *p2 = (const task_t *) prio_queue_at(&s_q, 2U);

    TEST_ASSERT_EQUAL_STRING("a", p0->name);
    TEST_ASSERT_EQUAL_STRING("b", p1->name);
    TEST_ASSERT_EQUAL_STRING("c", p2->name);
}

void test_fifo_mixed_priorities_equal_group_fifo(void)
{
    /* Вставляем: high(1), eq_a(2), eq_b(2), low(3) */
    task_t high = { .priority = 1, .name = "high" };
    task_t eq_a = { .priority = 2, .name = "eq_a" };
    task_t eq_b = { .priority = 2, .name = "eq_b" };
    task_t low  = { .priority = 3, .name = "low" };

    prio_queue_insert(&s_q, &high);
    prio_queue_insert(&s_q, &eq_a);
    prio_queue_insert(&s_q, &eq_b);
    prio_queue_insert(&s_q, &low);

    /* Ожидаемый порядок: high, eq_a, eq_b, low */
    const task_t *p0 = (const task_t *) prio_queue_at(&s_q, 0U);
    const task_t *p1 = (const task_t *) prio_queue_at(&s_q, 1U);
    const task_t *p2 = (const task_t *) prio_queue_at(&s_q, 2U);
    const task_t *p3 = (const task_t *) prio_queue_at(&s_q, 3U);

    TEST_ASSERT_EQUAL_STRING("high", p0->name);
    TEST_ASSERT_EQUAL_STRING("eq_a", p1->name);
    TEST_ASSERT_EQUAL_STRING("eq_b", p2->name);
    TEST_ASSERT_EQUAL_STRING("low", p3->name);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: full_eviction — статусы при заполнении
 * ══════════════════════════════════════════════════════════════════════════ */

static void fill_queue_with_priority(int priority)
{
    for (uint8_t i = 0U; i < CAPACITY; i++)
    {
        task_t t = { .priority = priority, .name = "filler" };
        prio_queue_insert(&s_q, &t);
    }
}

void test_full_returns_pq_ok_until_capacity(void)
{
    for (uint8_t i = 0U; i < CAPACITY; i++)
    {
        task_t t        = { .priority = (int) i, .name = "t" };
        pq_status_t ret = prio_queue_insert(&s_q, &t);
        TEST_ASSERT_EQUAL(PQ_OK, ret);
    }
    TEST_ASSERT_EQUAL_UINT8(CAPACITY, prio_queue_size(&s_q));
}

void test_full_higher_priority_returns_pq_evicted(void)
{
    fill_queue_with_priority(10);

    task_t better   = { .priority = 1, .name = "better" };
    pq_status_t ret = prio_queue_insert(&s_q, &better);

    TEST_ASSERT_EQUAL(PQ_EVICTED, ret);
    /* Размер не изменился */
    TEST_ASSERT_EQUAL_UINT8(CAPACITY, prio_queue_size(&s_q));
}

void test_full_lower_priority_returns_pq_full(void)
{
    fill_queue_with_priority(1);

    task_t worst    = { .priority = 100, .name = "worst" };
    pq_status_t ret = prio_queue_insert(&s_q, &worst);

    TEST_ASSERT_EQUAL(PQ_FULL, ret);
    TEST_ASSERT_EQUAL_UINT8(CAPACITY, prio_queue_size(&s_q));
}

void test_full_equal_priority_to_all_returns_pq_full(void)
{
    /* Все элементы с prio=5; новый тоже prio=5 → FIFO: новый менее приоритетен */
    fill_queue_with_priority(5);

    task_t same     = { .priority = 5, .name = "same" };
    pq_status_t ret = prio_queue_insert(&s_q, &same);

    TEST_ASSERT_EQUAL(PQ_FULL, ret);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: evict_correct — правильный элемент вытесняется
 * ══════════════════════════════════════════════════════════════════════════ */

void test_evict_removes_least_prioritized_element(void)
{
    /* Очередь: [1, 2, 3]; вставляем 2 → вытесняется 3 */
    task_t small_q_storage[3];
    memset(small_q_storage, 0, sizeof(small_q_storage));

    prio_queue_t small_q;
    prio_queue_init(&small_q, small_q_storage, 3U, sizeof(task_t), task_cmp);

    task_t a = { .priority = 1, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };
    task_t c = { .priority = 3, .name = "c" };

    prio_queue_insert(&small_q, &a);
    prio_queue_insert(&small_q, &b);
    prio_queue_insert(&small_q, &c);

    task_t d        = { .priority = 2, .name = "d" };
    pq_status_t ret = prio_queue_insert(&small_q, &d);

    TEST_ASSERT_EQUAL(PQ_EVICTED, ret);

    /* c(3) должна была вытесниться; d(2) должна быть внутри */
    bool found_d = false;
    bool found_c = false;

    for (uint8_t i = 0U; i < prio_queue_size(&small_q); i++)
    {
        const task_t *p_elem = (const task_t *) prio_queue_at(&small_q, i);
        if (strcmp(p_elem->name, "d") == 0)
        {
            found_d = true;
        }
        if (strcmp(p_elem->name, "c") == 0)
        {
            found_c = true;
        }
    }

    TEST_ASSERT_TRUE(found_d);
    TEST_ASSERT_FALSE(found_c);
}

void test_evict_top_still_correct_after_eviction(void)
{
    task_t small_q_storage[2];
    memset(small_q_storage, 0, sizeof(small_q_storage));

    prio_queue_t small_q;
    prio_queue_init(&small_q, small_q_storage, 2U, sizeof(task_t), task_cmp);

    task_t lo   = { .priority = 10, .name = "lo" };
    task_t hi   = { .priority = 1, .name = "hi" };
    task_t best = { .priority = 0, .name = "best" };

    prio_queue_insert(&small_q, &lo);
    prio_queue_insert(&small_q, &hi);
    /* best вытесняет lo */
    prio_queue_insert(&small_q, &best);

    const task_t *p_top = (const task_t *) prio_queue_peek(&small_q);
    TEST_ASSERT_NOT_NULL(p_top);
    TEST_ASSERT_EQUAL_STRING("best", p_top->name);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: peek — стабильность указателя и edge-cases
 * ══════════════════════════════════════════════════════════════════════════ */

void test_peek_null_when_empty(void)
{
    TEST_ASSERT_NULL(prio_queue_peek(&s_q));
}

void test_peek_does_not_remove_element(void)
{
    task_t t = { .priority = 3, .name = "t" };
    prio_queue_insert(&s_q, &t);

    prio_queue_peek(&s_q);
    prio_queue_peek(&s_q);

    TEST_ASSERT_EQUAL_UINT8(1U, prio_queue_size(&s_q));
}

void test_peek_pointer_into_internal_storage(void)
{
    task_t t = { .priority = 7, .name = "stored" };
    prio_queue_insert(&s_q, &t);

    const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
    TEST_ASSERT_EQUAL_STRING("stored", p_top->name);
    TEST_ASSERT_EQUAL_INT(7, p_top->priority);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: remove_at — корректность удаления и сдвига
 * ══════════════════════════════════════════════════════════════════════════ */

void test_remove_at_first_element(void)
{
    task_t a = { .priority = 1, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };
    task_t c = { .priority = 3, .name = "c" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);
    prio_queue_insert(&s_q, &c);

    prio_queue_remove_at(&s_q, 0U);

    TEST_ASSERT_EQUAL_UINT8(2U, prio_queue_size(&s_q));
    const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
    TEST_ASSERT_EQUAL_STRING("b", p_top->name);
}

void test_remove_at_middle_element(void)
{
    task_t a = { .priority = 1, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };
    task_t c = { .priority = 3, .name = "c" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);
    prio_queue_insert(&s_q, &c);

    prio_queue_remove_at(&s_q, 1U); /* удаляем b */

    TEST_ASSERT_EQUAL_UINT8(2U, prio_queue_size(&s_q));

    const task_t *p0 = (const task_t *) prio_queue_at(&s_q, 0U);
    const task_t *p1 = (const task_t *) prio_queue_at(&s_q, 1U);
    TEST_ASSERT_EQUAL_STRING("a", p0->name);
    TEST_ASSERT_EQUAL_STRING("c", p1->name);
}

void test_remove_at_last_element(void)
{
    task_t a = { .priority = 1, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);

    prio_queue_remove_at(&s_q, 1U);

    TEST_ASSERT_EQUAL_UINT8(1U, prio_queue_size(&s_q));
    const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
    TEST_ASSERT_EQUAL_STRING("a", p_top->name);
}

void test_remove_at_out_of_bounds_is_nop(void)
{
    task_t t = { .priority = 1, .name = "t" };
    prio_queue_insert(&s_q, &t);

    prio_queue_remove_at(&s_q, 5U); /* idx >= count — должна быть NOP */

    TEST_ASSERT_EQUAL_UINT8(1U, prio_queue_size(&s_q));
}

void test_remove_at_on_empty_queue_is_nop(void)
{
    prio_queue_remove_at(&s_q, 0U); /* не должно упасть */
    TEST_ASSERT_EQUAL_UINT8(0U, prio_queue_size(&s_q));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: at — доступ по индексу
 * ══════════════════════════════════════════════════════════════════════════ */

void test_at_returns_null_for_out_of_bounds(void)
{
    task_t t = { .priority = 1, .name = "t" };
    prio_queue_insert(&s_q, &t);

    TEST_ASSERT_NULL(prio_queue_at(&s_q, 1U));
    TEST_ASSERT_NULL(prio_queue_at(&s_q, 99U));
}

void test_at_returns_correct_element(void)
{
    task_t a = { .priority = 1, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);

    task_t *p_elem = (task_t *) prio_queue_at(&s_q, 1U);
    TEST_ASSERT_NOT_NULL(p_elem);
    TEST_ASSERT_EQUAL_STRING("b", p_elem->name);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: size — счётчик
 * ══════════════════════════════════════════════════════════════════════════ */

void test_size_increments_on_insert(void)
{
    for (uint8_t i = 0U; i < 4U; i++)
    {
        task_t t = { .priority = (int) i, .name = "t" };
        prio_queue_insert(&s_q, &t);
        TEST_ASSERT_EQUAL_UINT8((uint8_t) (i + 1U), prio_queue_size(&s_q));
    }
}

void test_size_decrements_on_remove(void)
{
    task_t a = { .priority = 1, .name = "a" };
    task_t b = { .priority = 2, .name = "b" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &b);

    prio_queue_remove_at(&s_q, 0U);
    TEST_ASSERT_EQUAL_UINT8(1U, prio_queue_size(&s_q));

    prio_queue_remove_at(&s_q, 0U);
    TEST_ASSERT_EQUAL_UINT8(0U, prio_queue_size(&s_q));
}

void test_size_unchanged_on_pq_full(void)
{
    fill_queue_with_priority(5);
    TEST_ASSERT_EQUAL_UINT8(CAPACITY, prio_queue_size(&s_q));

    task_t worst = { .priority = 100, .name = "worst" };
    prio_queue_insert(&s_q, &worst);

    TEST_ASSERT_EQUAL_UINT8(CAPACITY, prio_queue_size(&s_q));
}

/* ══════════════════════════════════════════════════════════════════════════
 * Группа: stress — последовательность вставок/удалений
 * ══════════════════════════════════════════════════════════════════════════ */

void test_stress_insert_all_remove_all_in_order(void)
{
    /* Вставляем 8 элементов с приоритетами 8..1 (убывающими) */
    for (int i = (int) CAPACITY; i >= 1; i--)
    {
        task_t t = { .priority = i, .name = "x" };
        prio_queue_insert(&s_q, &t);
    }

    /* Вытаскиваем по одному: каждый раз первый должен быть минимальным приоритетом */
    for (int expected = 1; expected <= (int) CAPACITY; expected++)
    {
        const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
        TEST_ASSERT_NOT_NULL(p_top);
        TEST_ASSERT_EQUAL_INT(expected, p_top->priority);
        prio_queue_remove_at(&s_q, 0U);
    }

    TEST_ASSERT_EQUAL_UINT8(0U, prio_queue_size(&s_q));
    TEST_ASSERT_NULL(prio_queue_peek(&s_q));
}

void test_stress_fifo_across_removes(void)
{
    /* Вставляем поочерёдно prio=1 (a, b, c) и между ними prio=2 */
    task_t a   = { .priority = 1, .name = "a" };
    task_t mid = { .priority = 2, .name = "mid" };
    task_t b   = { .priority = 1, .name = "b" };
    task_t c   = { .priority = 1, .name = "c" };

    prio_queue_insert(&s_q, &a);
    prio_queue_insert(&s_q, &mid);
    prio_queue_insert(&s_q, &b);
    prio_queue_insert(&s_q, &c);

    /* Порядок: a(1), b(1), c(1), mid(2) */
    const char *expected_names[] = { "a", "b", "c", "mid" };

    for (uint8_t i = 0U; i < 4U; i++)
    {
        const task_t *p_top = (const task_t *) prio_queue_peek(&s_q);
        TEST_ASSERT_NOT_NULL(p_top);
        TEST_ASSERT_EQUAL_STRING(expected_names[i], p_top->name);
        prio_queue_remove_at(&s_q, 0U);
    }
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    UNITY_BEGIN();

    /* init */
    RUN_TEST(test_init_size_is_zero);
    RUN_TEST(test_init_peek_returns_null);
    RUN_TEST(test_init_at_returns_null_when_empty);

    /* insert_order */
    RUN_TEST(test_insert_single_element);
    RUN_TEST(test_insert_two_ascending_order_peek_is_higher);
    RUN_TEST(test_insert_descending_input_peek_is_still_highest);
    RUN_TEST(test_insert_ascending_input_order_preserved);

    /* fifo */
    RUN_TEST(test_fifo_two_equal_priority_first_in_first_out);
    RUN_TEST(test_fifo_three_equal_priority_order_preserved);
    RUN_TEST(test_fifo_mixed_priorities_equal_group_fifo);

    /* full_eviction */
    RUN_TEST(test_full_returns_pq_ok_until_capacity);
    RUN_TEST(test_full_higher_priority_returns_pq_evicted);
    RUN_TEST(test_full_lower_priority_returns_pq_full);
    RUN_TEST(test_full_equal_priority_to_all_returns_pq_full);

    /* evict_correct */
    RUN_TEST(test_evict_removes_least_prioritized_element);
    RUN_TEST(test_evict_top_still_correct_after_eviction);

    /* peek */
    RUN_TEST(test_peek_null_when_empty);
    RUN_TEST(test_peek_does_not_remove_element);
    RUN_TEST(test_peek_pointer_into_internal_storage);

    /* remove_at */
    RUN_TEST(test_remove_at_first_element);
    RUN_TEST(test_remove_at_middle_element);
    RUN_TEST(test_remove_at_last_element);
    RUN_TEST(test_remove_at_out_of_bounds_is_nop);
    RUN_TEST(test_remove_at_on_empty_queue_is_nop);

    /* at */
    RUN_TEST(test_at_returns_null_for_out_of_bounds);
    RUN_TEST(test_at_returns_correct_element);

    /* size */
    RUN_TEST(test_size_increments_on_insert);
    RUN_TEST(test_size_decrements_on_remove);
    RUN_TEST(test_size_unchanged_on_pq_full);

    /* stress */
    RUN_TEST(test_stress_insert_all_remove_all_in_order);
    RUN_TEST(test_stress_fifo_across_removes);

    return UNITY_END();
}
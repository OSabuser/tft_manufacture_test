/**
 * @file  test_ring_buffer.c
 * @brief Unit-тесты для ring_buffer (Unity framework).
 *
 * Группы тестов:
 *   1. Init        — корректная и некорректная инициализация
 *   2. Put/Get     — базовые операции с одним байтом
 *   3. Write/Read  — блочные операции
 *   4. Full/Empty  — граничные состояния
 *   5. Wraparound  — корректность при переполнении индексов
 *   6. SPSC sim    — имитация одновременной записи ISR и чтения consumer
 *   7. Reset       — сброс состояния
 */

#include "ring_buffer/ring_buffer.h"
#include "unity.h"

#include <string.h>

/* ── Вспомогательные объекты ─────────────────────────────────────────── */

#define BUF_SIZE 8U /* Степень двойки, удобна для wraparound-тестов. */

static ring_buffer_desc_t rb;
static uint8_t storage[BUF_SIZE];

void setUp(void)
{
    /* Вызывается перед каждым тестом — чистое состояние. */
    bool ok = ring_buffer_init(&rb, storage, BUF_SIZE);
    TEST_ASSERT_TRUE(ok);
}

void tearDown(void)
{ /* ничего */
}

/* ═══════════════════════════════════════════════════════════════════════
 * 1. Init
 * ═══════════════════════════════════════════════════════════════════════ */

void test_init_valid(void)
{
    ring_buffer_desc_t tmp;
    uint8_t mem[16];
    TEST_ASSERT_TRUE(ring_buffer_init(&tmp, mem, 16U));
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&tmp));
    TEST_ASSERT_EQUAL_size_t(0U, ring_buffer_count(&tmp));
    TEST_ASSERT_EQUAL_size_t(16U, ring_buffer_free(&tmp));
}

void test_init_rejects_null_rb(void)
{
    uint8_t mem[8];
    TEST_ASSERT_FALSE(ring_buffer_init(NULL, mem, 8U));
}

void test_init_rejects_null_buf(void)
{
    ring_buffer_desc_t tmp;
    TEST_ASSERT_FALSE(ring_buffer_init(&tmp, NULL, 8U));
}

void test_init_rejects_zero_size(void)
{
    ring_buffer_desc_t tmp;
    uint8_t mem[8];
    TEST_ASSERT_FALSE(ring_buffer_init(&tmp, mem, 0U));
}

void test_init_rejects_non_power_of_two(void)
{
    ring_buffer_desc_t tmp;
    uint8_t mem[10];

    TEST_ASSERT_FALSE(ring_buffer_init(&tmp, mem, 3U));
    TEST_ASSERT_FALSE(ring_buffer_init(&tmp, mem, 5U));
    TEST_ASSERT_FALSE(ring_buffer_init(&tmp, mem, 6U));
    TEST_ASSERT_FALSE(ring_buffer_init(&tmp, mem, 7U));
}

void test_init_accepts_all_valid_powers_of_two(void)
{
    ring_buffer_desc_t tmp;
    uint8_t mem[256];

    for (size_t s = 1U; s <= 256U; s <<= 1U)
    {
        TEST_ASSERT_TRUE(ring_buffer_init(&tmp, mem, s));
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * 2. Put / Get — одиночные байты
 * ═══════════════════════════════════════════════════════════════════════ */

void test_put_get_single_byte(void)
{
    uint8_t out = 0xFF;

    TEST_ASSERT_TRUE(ring_buffer_put(&rb, 0xAB));
    TEST_ASSERT_EQUAL_size_t(1U, ring_buffer_count(&rb));
    TEST_ASSERT_FALSE(ring_buffer_is_empty(&rb));

    TEST_ASSERT_TRUE(ring_buffer_get(&rb, &out));
    TEST_ASSERT_EQUAL_UINT8(0xAB, out);
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

void test_get_from_empty_returns_false(void)
{
    uint8_t out = 0xAA;
    TEST_ASSERT_FALSE(ring_buffer_get(&rb, &out));
    /* out не должен быть изменён */
    TEST_ASSERT_EQUAL_UINT8(0xAA, out);
}

void test_put_returns_false_when_full(void)
{
    /* Заполнить буфер до краёв */
    for (uint8_t i = 0; i < BUF_SIZE; i++)
    {
        TEST_ASSERT_TRUE(ring_buffer_put(&rb, i));
    }
    TEST_ASSERT_TRUE(ring_buffer_is_full(&rb));

    /* Следующий put должен провалиться */
    TEST_ASSERT_FALSE(ring_buffer_put(&rb, 0xFF));
    /* Данные при этом не повреждены */
    TEST_ASSERT_EQUAL_size_t(BUF_SIZE, ring_buffer_count(&rb));
}

void test_fifo_ordering(void)
{
    /* Проверяем порядок FIFO: первым вошёл — первым вышел. */
    for (uint8_t i = 1U; i <= 4U; i++)
    {
        ring_buffer_put(&rb, i);
    }

    uint8_t out;
    for (uint8_t i = 1U; i <= 4U; i++)
    {
        TEST_ASSERT_TRUE(ring_buffer_get(&rb, &out));
        TEST_ASSERT_EQUAL_UINT8(i, out);
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * 3. Write / Read — блочные операции
 * ═══════════════════════════════════════════════════════════════════════ */

void test_write_read_full_block(void)
{
    const uint8_t src[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t dst[4]      = { 0 };

    TEST_ASSERT_EQUAL_size_t(4U, ring_buffer_write(&rb, src, 4U));
    TEST_ASSERT_EQUAL_size_t(4U, ring_buffer_count(&rb));

    TEST_ASSERT_EQUAL_size_t(4U, ring_buffer_read(&rb, dst, 4U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, 4U);
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

void test_write_partial_when_nearly_full(void)
{
    /* Заполнить 6 из 8 байт */
    uint8_t fill[6] = { 0 };
    ring_buffer_write(&rb, fill, 6U);
    TEST_ASSERT_EQUAL_size_t(2U, ring_buffer_free(&rb));

    /* Попытка записать 5, должно записаться 2 */
    const uint8_t src[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    size_t written      = ring_buffer_write(&rb, src, 5U);

    TEST_ASSERT_EQUAL_size_t(2U, written);
    TEST_ASSERT_TRUE(ring_buffer_is_full(&rb));
}

void test_read_partial_when_not_enough_data(void)
{
    ring_buffer_put(&rb, 0x11);
    ring_buffer_put(&rb, 0x22);

    uint8_t dst[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    size_t n       = ring_buffer_read(&rb, dst, 5U);

    /* Прочитано только то, что было */
    TEST_ASSERT_EQUAL_size_t(2U, n);
    TEST_ASSERT_EQUAL_UINT8(0x11, dst[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, dst[1]);
    /* Остаток dst не тронут */
    TEST_ASSERT_EQUAL_UINT8(0xFF, dst[2]);
}

void test_read_from_empty_returns_zero(void)
{
    uint8_t dst[4];
    TEST_ASSERT_EQUAL_size_t(0U, ring_buffer_read(&rb, dst, 4U));
}

/* ═══════════════════════════════════════════════════════════════════════
 * 4. Full / Empty — граничные состояния
 * ═══════════════════════════════════════════════════════════════════════ */

void test_empty_after_init(void)
{
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
    TEST_ASSERT_FALSE(ring_buffer_is_full(&rb));
    TEST_ASSERT_EQUAL_size_t(0U, ring_buffer_count(&rb));
    TEST_ASSERT_EQUAL_size_t(BUF_SIZE, ring_buffer_free(&rb));
}

void test_full_after_filling(void)
{
    for (uint8_t i = 0; i < BUF_SIZE; i++)
    {
        ring_buffer_put(&rb, i);
    }
    TEST_ASSERT_TRUE(ring_buffer_is_full(&rb));
    TEST_ASSERT_FALSE(ring_buffer_is_empty(&rb));
    TEST_ASSERT_EQUAL_size_t(BUF_SIZE, ring_buffer_count(&rb));
    TEST_ASSERT_EQUAL_size_t(0U, ring_buffer_free(&rb));
}

void test_not_full_after_one_read_from_full(void)
{
    for (uint8_t i = 0; i < BUF_SIZE; i++)
    {
        ring_buffer_put(&rb, i);
    }
    uint8_t out;
    ring_buffer_get(&rb, &out);

    TEST_ASSERT_FALSE(ring_buffer_is_full(&rb));
    TEST_ASSERT_EQUAL_size_t(BUF_SIZE - 1U, ring_buffer_count(&rb));
    TEST_ASSERT_EQUAL_size_t(1U, ring_buffer_free(&rb));
}

void test_empty_after_draining_full_buffer(void)
{
    for (uint8_t i = 0; i < BUF_SIZE; i++)
    {
        ring_buffer_put(&rb, i);
    }
    uint8_t out;
    for (size_t i = 0; i < BUF_SIZE; i++)
    {
        ring_buffer_get(&rb, &out);
    }
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

/* ═══════════════════════════════════════════════════════════════════════
 * 5. Wraparound — корректность при переполнении индексов
 *
 * Это самые важные тесты: именно здесь обычно живут баги.
 * Цикл: заполнить → слить → заполнить снова — индексы пройдут через
 * границу массива, маска & должна корректно их завернуть.
 * ═══════════════════════════════════════════════════════════════════════ */

void test_wraparound_single_bytes(void)
{
    uint8_t out;

    /* Несколько полных циклов fill→drain, чтобы гарантированно
     * пройти wraparound несколько раз. */
    for (int cycle = 0; cycle < 4; cycle++)
    {
        for (uint8_t i = 0; i < BUF_SIZE; i++)
        {
            TEST_ASSERT_TRUE(ring_buffer_put(&rb, i));
        }
        for (uint8_t i = 0; i < BUF_SIZE; i++)
        {
            TEST_ASSERT_TRUE(ring_buffer_get(&rb, &out));
            TEST_ASSERT_EQUAL_UINT8(i, out);
        }
        TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
    }
}

void test_wraparound_partial_overlap(void)
{
    uint8_t out;

    /* Сдвинуть индексы к краю массива: записать 6, прочитать 6. */
    for (uint8_t i = 0; i < 6U; i++)
        ring_buffer_put(&rb, i);
    for (uint8_t i = 0; i < 6U; i++)
        ring_buffer_get(&rb, &out);

    /* Теперь tail≈6, head≈6. Записать 5 — запись перейдёт через край. */
    const uint8_t src[] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE };
    TEST_ASSERT_EQUAL_size_t(5U, ring_buffer_write(&rb, src, 5U));

    /* Прочитать обратно — порядок должен сохраниться. */
    uint8_t dst[5];
    TEST_ASSERT_EQUAL_size_t(5U, ring_buffer_read(&rb, dst, 5U));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(src, dst, 5U);
}

void test_wraparound_preserves_data_integrity(void)
{
    /* Заполнить буфер контрольными значениями, слить наполовину,
     * дозаписать новые данные — проверить весь порядок. */
    for (uint8_t i = 0; i < BUF_SIZE; i++)
    {
        ring_buffer_put(&rb, i * 10U); /* 0, 10, 20, 30, 40, 50, 60, 70 */
    }

    uint8_t out;
    /* Слить первые 4 */
    for (uint8_t i = 0; i < 4U; i++)
    {
        ring_buffer_get(&rb, &out);
        TEST_ASSERT_EQUAL_UINT8(i * 10U, out);
    }

    /* Дозаписать 4 новых значения */
    for (uint8_t i = 0; i < 4U; i++)
    {
        ring_buffer_put(&rb, 0xA0U + i);
    }

    /* Прочитать оставшиеся старые 4 */
    for (uint8_t i = 4U; i < BUF_SIZE; i++)
    {
        ring_buffer_get(&rb, &out);
        TEST_ASSERT_EQUAL_UINT8(i * 10U, out);
    }

    /* Прочитать новые 4 */
    for (uint8_t i = 0; i < 4U; i++)
    {
        ring_buffer_get(&rb, &out);
        TEST_ASSERT_EQUAL_UINT8(0xA0U + i, out);
    }

    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

/* ═══════════════════════════════════════════════════════════════════════
 * 6. SPSC simulation — имитация ISR-producer / task-consumer
 *
 * В реальном коде ISR пишет байты, задача читает.
 * Здесь симулируем это в один поток, но чередуя операции —
 * проверяем, что счётчики не расходятся при долгой работе.
 * ═══════════════════════════════════════════════════════════════════════ */

void test_spsc_interleaved_puts_and_gets(void)
{
    uint8_t out;
    uint8_t expected = 0U;
    uint8_t next_in  = 0U;

    /* 200 итераций: ISR кладёт 3, задача забирает 2.
     * Буфер постепенно заполняется, потом стабилизируется у верхней границы.
     * Главное — порядок данных сохранён. */
    for (int i = 0; i < 200; i++)
    {
        /* "ISR": положить до 3 байт если есть место */
        for (int p = 0; p < 3; p++)
        {
            if (!ring_buffer_is_full(&rb))
            {
                ring_buffer_put(&rb, next_in++);
            }
        }
        /* "Task": забрать до 2 байт */
        for (int c = 0; c < 2; c++)
        {
            if (!ring_buffer_is_empty(&rb))
            {
                ring_buffer_get(&rb, &out);
                TEST_ASSERT_EQUAL_UINT8(expected++, out);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * 7. Reset
 * ═══════════════════════════════════════════════════════════════════════ */

void test_reset_clears_data(void)
{
    ring_buffer_put(&rb, 0xDE);
    ring_buffer_put(&rb, 0xAD);
    TEST_ASSERT_EQUAL_size_t(2U, ring_buffer_count(&rb));

    ring_buffer_reset(&rb);

    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
    TEST_ASSERT_EQUAL_size_t(0U, ring_buffer_count(&rb));
    TEST_ASSERT_EQUAL_size_t(BUF_SIZE, ring_buffer_free(&rb));
}

void test_reset_allows_reuse(void)
{
    /* Заполнить, сбросить, снова использовать — не должно быть артефактов. */
    for (uint8_t i = 0; i < BUF_SIZE; i++)
        ring_buffer_put(&rb, i);
    ring_buffer_reset(&rb);

    ring_buffer_put(&rb, 0x42);
    uint8_t out = 0;
    ring_buffer_get(&rb, &out);
    TEST_ASSERT_EQUAL_UINT8(0x42, out);
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

/* ── Runner ──────────────────────────────────────────────────────────── */

int main(void)
{
    UNITY_BEGIN();

    /* Init */
    RUN_TEST(test_init_valid);
    RUN_TEST(test_init_rejects_null_rb);
    RUN_TEST(test_init_rejects_null_buf);
    RUN_TEST(test_init_rejects_zero_size);
    RUN_TEST(test_init_rejects_non_power_of_two);
    RUN_TEST(test_init_accepts_all_valid_powers_of_two);

    /* Put / Get */
    RUN_TEST(test_put_get_single_byte);
    RUN_TEST(test_get_from_empty_returns_false);
    RUN_TEST(test_put_returns_false_when_full);
    RUN_TEST(test_fifo_ordering);

    /* Write / Read */
    RUN_TEST(test_write_read_full_block);
    RUN_TEST(test_write_partial_when_nearly_full);
    RUN_TEST(test_read_partial_when_not_enough_data);
    RUN_TEST(test_read_from_empty_returns_zero);

    /* Full / Empty */
    RUN_TEST(test_empty_after_init);
    RUN_TEST(test_full_after_filling);
    RUN_TEST(test_not_full_after_one_read_from_full);
    RUN_TEST(test_empty_after_draining_full_buffer);

    /* Wraparound */
    RUN_TEST(test_wraparound_single_bytes);
    RUN_TEST(test_wraparound_partial_overlap);
    RUN_TEST(test_wraparound_preserves_data_integrity);

    /* SPSC simulation */
    RUN_TEST(test_spsc_interleaved_puts_and_gets);

    /* Reset */
    RUN_TEST(test_reset_clears_data);
    RUN_TEST(test_reset_allows_reuse);

    return UNITY_END();
}
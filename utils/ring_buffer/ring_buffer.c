/**
 * @file  ring_buffer.c
 * @brief Реализация кольцевого буфера (SPSC, lock-free).
 *
 * Ключевой инвариант SPSC-безопасности:
 *   - producer читает head (чужой индекс) только для проверки is_full.
 *   - consumer читает tail (чужой индекс) только для проверки is_empty/count.
 *   - Каждый индекс пишет только его владелец.
 *   - volatile гарантирует, что компилятор не кэширует чужой индекс.
 *   - Беззнаковый wraparound size_t корректен: (0 - 1) == SIZE_MAX,
 *     арифметика modulo 2^N работает без явной обёртки.
 */

#include "ring_buffer.h"

#include <string.h>

/* ── Вспомогательная функция ─────────────────────────────────────────── */

/** Проверить, является ли n степенью двойки (и n > 0). */
static inline bool is_power_of_two(size_t n)
{
    return (n > 0U) && ((n & (n - 1U)) == 0U);
}

/* ── Инициализация ───────────────────────────────────────────────────── */

bool ring_buffer_init(ring_buffer_desc_t *p_desc, uint8_t *p_buf, size_t size)
{
    if (p_desc == NULL || p_buf == NULL || !is_power_of_two(size))
    {
        return false;
    }

    p_desc->buf  = p_buf;
    p_desc->size = size;
    p_desc->mask = size - 1U;
    p_desc->tail = 0U;
    p_desc->head = 0U;

    return true;
}

void ring_buffer_reset(ring_buffer_desc_t *p_desc)
{
    p_desc->tail = 0U;
    p_desc->head = 0U;
}

/* ── Состояние ───────────────────────────────────────────────────────── */

bool ring_buffer_is_empty(const ring_buffer_desc_t *p_desc)
{
    return p_desc->tail == p_desc->head;
}

bool ring_buffer_is_full(const ring_buffer_desc_t *p_desc)
{
    return (p_desc->tail - p_desc->head) == p_desc->size;
}

size_t ring_buffer_count(const ring_buffer_desc_t *p_desc)
{
    /* Беззнаковое вычитание: корректно при любом wraparound. */
    return p_desc->tail - p_desc->head;
}

size_t ring_buffer_free(const ring_buffer_desc_t *p_desc)
{
    return p_desc->size - (p_desc->tail - p_desc->head);
}

/* ── Запись (producer) ───────────────────────────────────────────────── */

bool ring_buffer_put(ring_buffer_desc_t *p_desc, uint8_t byte)
{
    if (ring_buffer_is_full(p_desc))
    {
        return false;
    }

    /*
     * Порядок важен для SPSC:
     *   1. Записать данные в ячейку.
     *   2. Только потом продвинуть tail — это делает байт видимым consumer-у.
     *
     * На Cortex-M7 (strongly-ordered memory model) барьер не нужен.
     * На weakly-ordered архитектурах (ARM64, RISC-V) нужен store-release.
     * Если понадобится портируемость — добавить __atomic_store_n.
     */
    p_desc->buf[p_desc->tail & p_desc->mask] = byte;
    p_desc->tail++;

    return true;
}

size_t ring_buffer_write(ring_buffer_desc_t *p_desc, const uint8_t *p_data, size_t len)
{
    size_t written = 0U;

    while (written < len && !ring_buffer_is_full(p_desc))
    {
        p_desc->buf[p_desc->tail & p_desc->mask] = p_data[written];
        p_desc->tail++;
        written++;
    }

    return written;
}

/* ── Чтение (consumer) ───────────────────────────────────────────────── */

bool ring_buffer_get(ring_buffer_desc_t *p_desc, uint8_t *p_byte)
{
    if (ring_buffer_is_empty(p_desc))
    {
        return false;
    }

    /*
     * Симметрично put():
     *   1. Прочитать данные из ячейки.
     *   2. Продвинуть head — освобождает место для producer-а.
     */
    *p_byte = p_desc->buf[p_desc->head & p_desc->mask];
    p_desc->head++;

    return true;
}

size_t ring_buffer_read(ring_buffer_desc_t *p_desc, uint8_t *p_data, size_t len)
{
    size_t read = 0U;

    while (read < len && !ring_buffer_is_empty(p_desc))
    {
        p_data[read] = p_desc->buf[p_desc->head & p_desc->mask];
        p_desc->head++;
        read++;
    }

    return read;
}
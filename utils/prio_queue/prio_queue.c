/*
 * prio_queue.c
 */

#include "prio_queue.h"

#include <string.h>

/* ---------- вспомогательные ---------- */

static void *pq_at(prio_queue_t *p_q, uint8_t i)
{
    return (uint8_t *) p_q->buf + (size_t) i * p_q->item_size;
}

static const void *pq_at_const(const prio_queue_t *p_q, uint8_t i)
{
    return (const uint8_t *) p_q->buf + (size_t) i * p_q->item_size;
}

/* ---------- публичный API ---------- */

void prio_queue_init(prio_queue_t *p_q, void *p_buf, uint8_t capacity, size_t item_size,
                     pq_cmp_fn p_cmp)
{
    p_q->buf       = p_buf;
    p_q->capacity  = capacity;
    p_q->count     = 0;
    p_q->item_size = item_size;
    p_q->cmp       = p_cmp;
    memset(p_buf, 0, (size_t) capacity * item_size);
}

pq_status_t prio_queue_insert(prio_queue_t *p_q, const void *p_item)
{
    /* Найти позицию вставки: первый элемент, перед которым новый стоит в очереди.
       cmp(existing, new) > 0 означает: existing менее приоритетен, вставляем перед ним. */
    uint8_t insert_pos = p_q->count;
    for (uint8_t i = 0; i < p_q->count; i++)
    {
        if (p_q->cmp(pq_at(p_q, i), p_item) > 0)
        {
            insert_pos = i;
            break;
        }
    }

    if (p_q->count < p_q->capacity)
    {
        /* Есть свободное место — сдвигаем вправо и вставляем */
        for (uint8_t i = p_q->count; i > insert_pos; i--)
        {
            memcpy(pq_at(p_q, i), pq_at(p_q, i - 1), p_q->item_size);
        }
        memcpy(pq_at(p_q, insert_pos), p_item, p_q->item_size);
        p_q->count++;
        return PQ_OK;
    }

    /* Буфер полон */
    if (insert_pos == p_q->capacity)
    {
        /* Новый элемент наименее приоритетен — отклоняем */
        return PQ_FULL;
    }

    /* Вытесняем последний (наименее приоритетный), сдвигаем, вставляем */
    for (uint8_t i = p_q->capacity - 1U; i > insert_pos; i--)
    {
        memcpy(pq_at(p_q, i), pq_at(p_q, i - 1), p_q->item_size);
    }
    memcpy(pq_at(p_q, insert_pos), p_item, p_q->item_size);
    return PQ_EVICTED;
}

const void *prio_queue_peek(const prio_queue_t *p_q)
{
    if (p_q->count == 0U)
    {
        return NULL;
    }
    return pq_at_const(p_q, 0);
}

uint8_t prio_queue_size(const prio_queue_t *p_q)
{
    return p_q->count;
}

void *prio_queue_at(prio_queue_t *p_q, uint8_t idx)
{
    if (idx >= p_q->count)
    {
        return NULL;
    }
    return pq_at(p_q, idx);
}

void prio_queue_remove_at(prio_queue_t *p_q, uint8_t idx)
{
    if (idx >= p_q->count)
    {
        return;
    }
    for (uint8_t i = idx; i < p_q->count - 1U; i++)
    {
        memcpy(pq_at(p_q, i), pq_at(p_q, i + 1), p_q->item_size);
    }
    memset(pq_at(p_q, p_q->count - 1U), 0, p_q->item_size);
    p_q->count--;
}
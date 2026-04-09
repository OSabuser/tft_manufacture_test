/*
 * prio_queue.h
 *
 * Generic sorted array с фиксированной ёмкостью.
 * Не знает ничего про аудио, FreeRTOS или FatFS.
 *
 * Порядок сортировки задаётся comparator-функцией:
 *   cmp(a, b) < 0  →  a стоит перед b (a приоритетнее)
 *   cmp(a, b) > 0  →  b стоит перед a
 *   cmp(a, b) == 0 →  равнозначны (порядок вставки сохраняется)
 */

#ifndef PRIO_QUEUE_H_
#define PRIO_QUEUE_H_

#include <stddef.h>
#include <stdint.h>

typedef int (*pq_cmp_fn)(const void *a, const void *b);

typedef enum
{
    PQ_OK,      /*!< Вставка прошла успешно                                  */
    PQ_EVICTED, /*!< Вставка прошла, наименее приоритетный элемент вытеснён   */
    PQ_FULL, /*!< Очередь полна, новый элемент менее приоритетен чем все   */
} pq_status_t;

typedef struct
{
    void *buf; /*!< Указатель на внешний буфер элементов           */
    uint8_t capacity; /*!< Максимальное число элементов                   */
    uint8_t count;    /*!< Текущее число элементов                        */
    size_t item_size; /*!< Размер одного элемента в байтах                */
    pq_cmp_fn cmp;    /*!< Функция сравнения                              */
} prio_queue_t;

/*!
 * @brief Инициализация. Буфер @buf должен быть выделен вызывающей стороной
 *        и иметь размер не менее @capacity * @item_size байт.
 */
void prio_queue_init(prio_queue_t *p_q, void *p_buf, uint8_t capacity, size_t item_size,
                     pq_cmp_fn p_cmp);

/*!
 * @brief Вставить элемент с сохранением сортировки.
 *        При полном буфере и более высоком приоритете — вытесняет последний.
 */
pq_status_t prio_queue_insert(prio_queue_t *p_q, const void *p_item);

/*!
 * @brief Вернуть указатель на элемент с наивысшим приоритетом (index 0).
 *        NULL если очередь пуста.
 */
const void *prio_queue_peek(const prio_queue_t *p_q);

/*! Текущее число элементов. */
uint8_t prio_queue_size(const prio_queue_t *p_q);

/*!
 * @brief Изменяемый доступ к элементу по индексу.
 *        NULL если @idx >= count.
 */
void *prio_queue_at(prio_queue_t *p_q, uint8_t idx);

/*!
 * @brief Удалить элемент по индексу со сдвигом остальных влево.
 */
void prio_queue_remove_at(prio_queue_t *p_q, uint8_t idx);

#endif /* PRIO_QUEUE_H_ */
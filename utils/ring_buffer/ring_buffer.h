/**
 * @file  ring_buffer.h
 * @brief Кольцевой буфер — SPSC, lock-free для Cortex-M и host.
 *
 * Конвенция индексов:
 *   tail — индекс следующей ЗАПИСИ  (двигает producer / ISR)
 *   head — индекс следующего ЧТЕНИЯ (двигает consumer / задача)
 *
 *   Запись:  buf[tail & mask] = byte; tail++;
 *   Чтение:  byte = buf[head & mask]; head++;
 *   Счётчик: tail - head  (беззнаковая арифметика, wraparound корректен)
 *
 *              запись                    чтение
 *                ↓                          ↓
 *   ... [ ][ ][tail][D][D][D][D][D][head][ ][ ] ...
 *                    ←───── данные ──────→
 *
 * Гарантии потокобезопасности:
 *   - Ровно один writer и один reader (SPSC).
 *   - Запись и чтение не требуют отключения прерываний.
 *   - head и tail объявлены volatile — компилятор не кэширует в регистрах.
 *   - На Cortex-M запись/чтение size_t (32 бит) атомарны по архитектуре.
 *
 * Ограничения:
 *   - size ОБЯЗАНА быть степенью двойки (проверяется в ring_buffer_init).
 *   - Элемент — ровно 1 байт. Для 9-bit UART используй uint8_t[2] снаружи.
 *   - Multi-producer/consumer — только с внешней синхронизацией.
 *
 * Платформо-независим: зависит только от <stdint.h>, <stddef.h>, <stdbool.h>.
 */
#ifndef RING_BUFFER_H_
#define RING_BUFFER_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    uint8_t *buf; /**< Указатель на внешний массив данных.        */
    size_t size;  /**< Размер массива (степень двойки).           */
    size_t mask;  /**< size - 1, для быстрого & вместо %.          */
    volatile size_t head; /**< Индекс следующего чтения (consumer). */
    volatile size_t tail; /**< Индекс следующей записи (producer).  */
} ring_buffer_desc_t;

/**
 * @brief Инициализировать дескриптор.
 *
 * @param p_desc    Дескриптор (не NULL).
 * @param p_buf   Внешний массив (не NULL, размер = @p size байт).
 * @param size  Размер массива — ОБЯЗАТЕЛЬНО степень двойки.
 * @return true при успехе, false если size не степень двойки.
 */
bool ring_buffer_init(ring_buffer_desc_t *p_desc, uint8_t *p_buf, size_t size);

/**
 * @brief Сбросить буфер в пустое состояние.
 *
 * @warning Не thread-safe. Вызов только когда ни producer,
 *          ни consumer не активны (например, при переинициализации).
 */
void ring_buffer_reset(ring_buffer_desc_t *p_desc);

/**
 * @brief Положить один байт.
 * @return true если байт записан, false если буфер полон.
 */
bool ring_buffer_put(ring_buffer_desc_t *p_desc, uint8_t byte);

/* ── Запись (producer side) ──────────────────────────────────────────── */
/**
 * @brief Положить блок байт.
 * @return Количество реально записанных байт (< len если буфер заполнился).
 */
size_t ring_buffer_write(ring_buffer_desc_t *p_desc, const uint8_t *p_data, size_t len);

/* ── Чтение (consumer side) ──────────────────────────────────────────── */

/**
 * @brief Прочитать один байт.
 * @return true если байт получен, false если буфер пуст.
 */
bool ring_buffer_get(ring_buffer_desc_t *p_desc, uint8_t *p_byte);

/**
 * @brief Прочитать блок байт.
 * @return Количество реально прочитанных байт.
 */
size_t ring_buffer_read(ring_buffer_desc_t *p_desc, uint8_t *p_data, size_t len);

/* ── Состояние ───────────────────────────────────────────────────────── */

/** @return true если буфер пуст (head == tail). */
bool ring_buffer_is_empty(const ring_buffer_desc_t *p_desc);

/** @return true если буфер полон (tail - head == size). */
bool ring_buffer_is_full(const ring_buffer_desc_t *p_desc);

/** @return Количество байт, доступных для чтения. */
size_t ring_buffer_count(const ring_buffer_desc_t *p_desc);

/** @return Количество байт, доступных для записи. */
size_t ring_buffer_free(const ring_buffer_desc_t *p_desc);

#endif //RING_BUFFER_H_
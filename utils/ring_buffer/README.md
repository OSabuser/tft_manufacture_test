# ring_buffer — SPSC кольцевой буфер байт

Lock-free кольцевой буфер для сценария единственный producer / единственный
consumer. Типичное использование: ISR пишет принятые байты, задача или main
loop читает — без отключения прерываний.

| Параметр      | Значение                                                                       |
| ------------- | ------------------------------------------------------------------------------ |
| Элемент       | 1 байт (`uint8_t`)                                                             |
| Ёмкость       | любая степень двойки, задаётся при `ring_buffer_init`                          |
| Thread-safety | SPSC без блокировок; multi-producer/consumer — только с внешней синхронизацией |
| Зависимости   | `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`                                      |

---

## API

```c
/* Инициализация */
bool ring_buffer_init(ring_buffer_desc_t *p_desc, uint8_t *p_buf, size_t size);
void ring_buffer_reset(ring_buffer_desc_t *p_desc);

/* Состояние */
bool   ring_buffer_is_empty(const ring_buffer_desc_t *p_desc);
bool   ring_buffer_is_full(const ring_buffer_desc_t *p_desc);
size_t ring_buffer_count(const ring_buffer_desc_t *p_desc);
size_t ring_buffer_free(const ring_buffer_desc_t *p_desc);

/* Запись (producer) */
bool   ring_buffer_put(ring_buffer_desc_t *p_desc, uint8_t byte);
size_t ring_buffer_write(ring_buffer_desc_t *p_desc, const uint8_t *p_data, size_t len);

/* Чтение (consumer) */
bool   ring_buffer_get(ring_buffer_desc_t *p_desc, uint8_t *p_byte);
size_t ring_buffer_read(ring_buffer_desc_t *p_desc, uint8_t *p_data, size_t len);
```

`ring_buffer_init` требует `size` — степень двойки; возвращает `false` при
невалидных аргументах. `ring_buffer_write` / `ring_buffer_read` возвращают
фактически переданное количество байт.

---

## Быстрый старт

```c
#include "ring_buffer/ring_buffer.h"

static uint8_t         storage[256];   /* степень двойки */
static ring_buffer_desc_t rb;

ring_buffer_init(&rb, storage, sizeof(storage));

/* Producer (например, из ISR): */
ring_buffer_put(&rb, received_byte);

/* Consumer (например, из main loop): */
uint8_t b;
if (ring_buffer_get(&rb, &b)) {
    /* обработать b */
}
```

---

## Тестирование

Host unit-тесты: `tests/host/ring_buffer/` — 24 теста, покрывают wraparound,
граничные значения и SPSC-симуляцию.

```bash
just build::test-host
```

---

## Примечания по реализации

**Wraparound.** Индексы `head` и `tail` — монотонно возрастающие `size_t`.
Маскирование через `& mask` (где `mask = size - 1`) даёт корректный индекс
ячейки. Беззнаковый wraparound арифметически корректен: `(0 - 1) == SIZE_MAX`,
подсчёт заполненности через `tail - head` работает без явной обёртки.

**Memory ordering.** На Cortex-M7 (strongly-ordered) барьер памяти не нужен.
На weakly-ordered архитектурах (ARM64, RISC-V) потребуется store-release /
load-acquire — добавить `__atomic_store_n` / `__atomic_load_n`.

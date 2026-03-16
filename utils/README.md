# utils

Платформо-независимые утилиты проекта.

**Правило включения** — код попадает сюда только если выполняются оба условия:

- не зависит от железа (нет `fsl_*`, CMSIS, FreeRTOS, BSP);
- используется более чем в одном месте проекта.

Все модули компилируются на хосте (host-тесты) и на таргете без изменений.

---

## Модули

### `ring_buffer`

Кольцевой буфер байт — SPSC (single-producer / single-consumer), lock-free.

**Типичное использование:** ISR пишет принятые байты, задача или основной цикл
читает. Не требует отключения прерываний при условии единственного producer и
единственного consumer.

| Параметр | Значение |
|---|---|
| Элемент | 1 байт (`uint8_t`) |
| Ёмкость | любая степень двойки, задаётся при `ring_buffer_init` |
| Thread-safety | SPSC без блокировок; multi-producer/consumer — только с внешней синхронизацией |
| Зависимости | `<stdint.h>`, `<stddef.h>`, `<stdbool.h>` |

```c
#include "ring_buffer/ring_buffer.h"

static uint8_t      storage[256];
static ring_buffer_t rb;

// Инициализация (размер — степень двойки)
ring_buffer_init(&rb, storage, sizeof(storage));

// Запись (например, из ISR)
ring_buffer_put(&rb, byte);

// Чтение (например, из задачи)
uint8_t b;
if (ring_buffer_get(&rb, &b)) { /* обработать b */ }
```

Тесты: `tests/host/test_ring_buffer.c` (24 теста, включая wraparound и SPSC-симуляцию).

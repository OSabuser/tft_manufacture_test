# prio_queue — приоритетная очередь с вытеснением

Отсортированный массив с фиксированной ёмкостью и generic элементами.
При полном буфере новый элемент с более высоким приоритетом вытесняет
наименее приоритетный. Оптимален для небольших очередей (n ≤ 32):
задачи, события, медиадорожки с приоритетами.

| Параметр      | Значение                                                                |
| ------------- | ----------------------------------------------------------------------- |
| Элемент       | любой тип, задаётся через `item_size`                                   |
| Ёмкость       | фиксирована на всё время жизни, задаётся при `prio_queue_init`          |
| Порядок       | определяется `cmp`-функцией пользователя (сигнатура как у `qsort`)      |
| Вставка       | O(n) сдвиг                                                              |
| Peek-top      | O(1)                                                                    |
| Thread-safety | нет; при использовании из нескольких контекстов — внешняя синхронизация |
| Зависимости   | `<stdint.h>`, `<stddef.h>`, `<string.h>`                                |

---

## API

```c
void        prio_queue_init(prio_queue_t *q, void *buf, uint8_t capacity,
                            size_t item_size, pq_cmp_fn cmp);

pq_status_t prio_queue_insert(prio_queue_t *q, const void *item);
const void *prio_queue_peek(const prio_queue_t *q);
uint8_t     prio_queue_size(const prio_queue_t *q);
void       *prio_queue_at(prio_queue_t *q, uint8_t idx);
void        prio_queue_remove_at(prio_queue_t *q, uint8_t idx);
```

**Коды возврата `prio_queue_insert`:**

| Код          | Условие                                               |
| ------------ | ----------------------------------------------------- |
| `PQ_OK`      | Вставлен, место было                                  |
| `PQ_EVICTED` | Вставлен, наименее приоритетный вытеснен              |
| `PQ_FULL`    | Отклонён — новый элемент наименее приоритетен из всех |

---

## Быстрый старт

```c
#include "prio_queue/prio_queue.h"

typedef struct { int priority; const char *name; } task_t;

/* Comparator: меньший priority → ближе к голове */
static int task_cmp(const void *a, const void *b)
{
    int pa = ((const task_t *)a)->priority;
    int pb = ((const task_t *)b)->priority;
    return (pa < pb) ? -1 : (pa > pb) ? 1 : 0;
}

static task_t       storage[8];
static prio_queue_t q;

prio_queue_init(&q, storage, 8, sizeof(task_t), task_cmp);

task_t t = { .priority = 2, .name = "send_data" };
pq_status_t st = prio_queue_insert(&q, &t);

const task_t *top = prio_queue_peek(&q);
if (top != NULL) { /* обработать */ }

prio_queue_remove_at(&q, 0);   /* удалить верхний после обработки */
```

**Политика вытеснения при полном буфере:**

```
Очередь полна [A(1) B(2) C(3)], вставляем D(2):
  → D приоритетнее C(3) → C вытесняется → [A(1) B(2) D(2)]   PQ_EVICTED

Очередь полна [A(1) B(2) C(3)], вставляем E(5):
  → E менее приоритетен чем все → отклоняется                 PQ_FULL
```

---

## Comparator

Сигнатура идентична `qsort`:

```c
typedef int (*pq_cmp_fn)(const void *a, const void *b);
```

| Возврат | Смысл                                    |
| ------- | ---------------------------------------- |
| `< 0`   | `a` стоит перед `b` (a приоритетнее)     |
| `> 0`   | `b` стоит перед `a`                      |
| `0`     | равнозначны; порядок вставки сохраняется |

---

## Тестирование

Host unit-тесты: `tests/host/prio_queue/`.

```bash
just build::test-host
```

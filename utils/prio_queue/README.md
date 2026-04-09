# util/prio_queue

Отсортированный массив с фиксированной ёмкостью — generic приоритетная очередь
с вытеснением.

**Типичное использование:** очередь задач, событий или медиадорожек с приоритетами,
где число элементов заранее известно и невелико (≤ ~32). При полном буфере новый
элемент с более высоким приоритетом вытесняет наименее приоритетный.

| Параметр | Значение |
|---|---|
| Элемент | любой тип, задаётся через `item_size` |
| Ёмкость | любая, задаётся при `prio_queue_init`; фиксирована на всё время жизни |
| Порядок | определяется `cmp`-функцией пользователя (аналог `qsort`) |
| Вставка | O(n) сдвиг; оптимально при n ≤ 32 |
| Peek-top | O(1) |
| Thread-safety | нет; при использовании из нескольких контекстов — внешняя синхронизация |
| Зависимости | `<stdint.h>`, `<stddef.h>`, `<string.h>` |

## Быстрый старт

```c
#include "prio_queue/prio_queue.h"

typedef struct { int priority; const char *name; } task_t;

/* Comparator: меньший priority → ближе к голове */
static int task_cmp(const void *a, const void *b) {
    int pa = ((const task_t *)a)->priority;
    int pb = ((const task_t *)b)->priority;
    return (pa < pb) ? -1 : (pa > pb) ? 1 : 0;
}

static task_t   storage[8];
static prio_queue_t q;

/* Инициализация */
prio_queue_init(&q, storage, 8, sizeof(task_t), task_cmp);

/* Вставка */
task_t t = { .priority = 2, .name = "send_data" };
pq_status_t st = prio_queue_insert(&q, &t);
/* st == PQ_OK      — вставлен
   st == PQ_EVICTED — вставлен, наименее приоритетный вытеснен
   st == PQ_FULL    — отклонён, новый элемент наименее приоритетен из всех */

/* Чтение верхнего элемента без удаления */
const task_t *top = prio_queue_peek(&q);
if (top != NULL) { /* обработать top */ }

/* Удаление верхнего элемента после обработки */
prio_queue_remove_at(&q, 0);
```

## Политика вытеснения при полном буфере

```
Очередь полна [A(1) B(2) C(3)], вставляем D(2):
  → D приоритетнее C(3)  → C вытесняется → [A(1) B(2) D(2)]   PQ_EVICTED

Очередь полна [A(1) B(2) C(3)], вставляем E(5):
  → E менее приоритетен чем все → отклоняется                  PQ_FULL
```

## Comparator

Сигнатура идентична `qsort`:

```c
typedef int (*pq_cmp_fn)(const void *a, const void *b);
```

| Возврат | Смысл |
|---|---|
| `< 0` | `a` стоит перед `b` (a приоритетнее) |
| `> 0` | `b` стоит перед `a` |
| `0` | равнозначны; порядок вставки сохраняется |

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

Тесты: `tests/host/test_prio_queue.c`

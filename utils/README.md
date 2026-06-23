# utils — платформонезависимые утилиты

Общие структуры данных и сервисы проекта. Код попадает сюда только если
выполняются оба условия:

- не зависит от железа (нет `fsl_*`, CMSIS, FreeRTOS, BSP);
- используется более чем в одном месте проекта.

Все модули компилируются на хосте (host-тесты) и на таргете без изменений.

---

## Модули

| Модуль        | Путь                                           | Описание                             |
| ------------- | ---------------------------------------------- | ------------------------------------ |
| `ring_buffer` | [ring_buffer/README.md](ring_buffer/README.md) | SPSC кольцевой буфер байт, lock-free |
| `prio_queue`  | [prio_queue/README.md](prio_queue/README.md)   | Приоритетная очередь с вытеснением   |
| `log`         | [log/README.md](log/README.md)                 | Логгер с callback-транспортом        |

---

## CMake

Все три модуля собираются в одну статическую библиотеку `utils`:

```cmake
target_link_libraries(<target> PRIVATE utils)
```

Корень `utils/` автоматически добавляется в include path — `#include`
указывает полный путь от корня:

```c
#include "ring_buffer/ring_buffer.h"
#include "prio_queue/prio_queue.h"
#include "log/log.h"
```

`LOG_LEVEL` пробрасывается из CMake-пресета или командной строки:

```cmake
target_compile_definitions(utils PUBLIC LOG_LEVEL=4)
```

Если не задан — `log.h` выбирает уровень сам через `NDEBUG`
(Verbose в Debug, Off в Release).

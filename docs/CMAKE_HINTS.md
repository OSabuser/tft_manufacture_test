# PRIVATE, PUBLIC и INTERFACE в CMake

## Основная идея

Каждый таргет в CMake — это «чёрный ящик» с двумя границами:

- **внутренняя** — то, что нужно только для компиляции самого таргета
- **внешняя** — то, что таргет «экспортирует» своим потребителям

Ключевые слова `PRIVATE`, `PUBLIC` и `INTERFACE` управляют тем, в какую из этих границ попадает свойство.

``````bash
┌─────────────────────────────────────────────────┐
│                   lib_a                         │
│                                                 │
│   PRIVATE          │        INTERFACE           │
│   (только внутри)  │        (только снаружи)    │
│                    │                            │
│        PUBLIC = PRIVATE + INTERFACE             │
└─────────────────────────────────────────────────┘
          │ target_link_libraries(app PRIVATE lib_a)
          ▼
       [ app ]  ← получает только INTERFACE-свойства lib_a
```

---

## Определения

| Ключевое слово | Применяется к самому таргету | Передаётся потребителям |
|----------------|:----------------------------:|:-----------------------:|
| `PRIVATE`      | ✅                           | ❌                      |
| `PUBLIC`       | ✅                           | ✅                      |
| `INTERFACE`    | ❌                           | ✅                      |

---

## Разбор на примерах

### Пример 1 — `target_include_directories`

``````bash
src/
├── lib_math/
│   ├── CMakeLists.txt
│   ├── include/         ← публичные заголовки (нужны потребителям)
│   │   └── math.h
│   ├── internal/        ← внутренние заголовки (только для lib_math)
│   │   └── impl.h
│   └── math.c
└── app/
    ├── CMakeLists.txt
    └── main.c           ← #include "math.h"
```

```cmake
# lib_math/CMakeLists.txt
add_library(lib_math STATIC math.c)

target_include_directories(lib_math
    PRIVATE   internal/   # impl.h нужен только при компиляции math.c
    PUBLIC    include/    # math.h нужен и lib_math, и всем её потребителям
)
```

```cmake
# app/CMakeLists.txt
add_library(app main.c)
target_link_libraries(app PRIVATE lib_math)
# app автоматически получает include/ через PUBLIC-свойство lib_math
# app НЕ получает internal/ — оно PRIVATE
```

**Итог:** `main.c` может писать `#include "math.h"`, но не видит `impl.h`.

---

### Пример 2 — `target_compile_definitions`

``````bash
lib_json  ──→  lib_http  ──→  app
```

```cmake
# lib_json
add_library(lib_json STATIC json.c)
target_compile_definitions(lib_json
    PRIVATE   JSON_INTERNAL_DEBUG   # дефайн только для json.c
    PUBLIC    JSON_VERSION=2        # нужен и json.c, и потребителям
)
```

```cmake
# lib_http линкуется с lib_json
add_library(lib_http STATIC http.c)
target_link_libraries(lib_http PUBLIC lib_json)
# lib_http транзитивно передаёт JSON_VERSION=2 дальше в app
```

```cmake
# app
add_executable(app main.c)
target_link_libraries(app PRIVATE lib_http)
# app видит JSON_VERSION=2 (транзитивно через lib_http → lib_json)
# app НЕ видит JSON_INTERNAL_DEBUG (PRIVATE)
```

---

### Пример 3 — `INTERFACE` (header-only библиотека)

`INTERFACE` используется тогда, когда таргет сам **не компилируется** — например, header-only библиотека или набор флагов.

```cmake
# Набор флагов для встроенных таргетов — сам не компилируется
add_library(flags_embedded INTERFACE)

target_compile_options(flags_embedded INTERFACE
    -mcpu=cortex-m7
    -mfpu=fpv5-d16
    -mfloat-abi=hard
    -mthumb
)

target_compile_definitions(flags_embedded INTERFACE
    ARM_MATH_CM7
    __FPU_PRESENT=1
)
```

```cmake
# Любой firmware-таргет подключает флаги одной строкой
target_link_libraries(firmware_app PRIVATE flags_embedded)
# firmware_app получает все -mcpu, -mfpu и дефайны
```

Такой паттерн часто используется для:

- HAL/SDK флагов конкретного МК
- Флагов линкера (`-T linker.ld`)
- Опций оптимизации для конкретного сценария сборки

---

### Пример 4 — транзитивность

Понять транзитивность важно: свойства распространяются по цепочке зависимостей.

```
lib_base  ──→  lib_mid  ──→  app
```

```cmake
add_library(lib_base STATIC base.c)
target_compile_definitions(lib_base
    PUBLIC    BASE_FEATURE_ENABLED   # пойдёт вглубь цепочки
    PRIVATE   BASE_INTERNAL          # остановится здесь
)

add_library(lib_mid STATIC mid.c)
target_link_libraries(lib_mid PUBLIC lib_base)
#                              ^^^^^^
# PUBLIC здесь означает: "lib_mid использует lib_base,
# и мои потребители тоже должны о ней знать"

add_executable(app main.c)
target_link_libraries(app PRIVATE lib_mid)
```

**Что видит `app`:**

| Дефайн                | Виден в app? | Причина                                          |
|-----------------------|:------------:|--------------------------------------------------|
| `BASE_FEATURE_ENABLED`| ✅           | PUBLIC в lib_base → PUBLIC в lib_mid → app       |
| `BASE_INTERNAL`       | ❌           | PRIVATE в lib_base, цепочка обрывается           |

Если бы `lib_mid` слинковался с `lib_base` через `PRIVATE`:

```cmake
target_link_libraries(lib_mid PRIVATE lib_base)
# тогда app НЕ увидел бы BASE_FEATURE_ENABLED — цепочка оборвалась бы на lib_mid
```

---

## Применимость к командам

Ключевые слова работают одинаково во всех `target_*` командах:

| Команда                        | Типичное использование                                   |
|--------------------------------|----------------------------------------------------------|
| `target_include_directories`   | PRIVATE — внутренние папки; PUBLIC — публичные заголовки |
| `target_compile_definitions`   | PRIVATE — отладочные дефайны; PUBLIC — версии API        |
| `target_compile_options`       | PRIVATE — флаги оптимизации; INTERFACE — флаги МК        |
| `target_link_libraries`        | PRIVATE — внутр. зависимость; PUBLIC — транзитивная      |
| `target_link_options`          | INTERFACE — скрипт линкера для всей цепочки              |
| `target_sources`               | Всегда PRIVATE — исходники не передаются потребителям    |

---

## Правило выбора

```bash
Нужно ли это свойство самому таргету?
        │
       ДА ──→  Нужно ли оно потребителям?
        │              │
        │             ДА ──→  PUBLIC
        │              │
        │             НЕТ ──→ PRIVATE
        │
       НЕТ ──→  Нужно ли оно потребителям?
                       │
                      ДА ──→  INTERFACE
                       │
                      НЕТ ──→ (не добавляйте это свойство вообще)
```

---

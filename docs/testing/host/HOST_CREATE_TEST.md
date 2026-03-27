# Добавление нового host-теста

## Обзор стека

```bash
devcontainer
─────────────────────────────────────────────────────────────────
tests/host/<n>/test_<n>.c      ← тест (Unity + опционально fff)
tests/host/CMakeLists.txt      ← регистрация через add_host_test()
tests/host/mocks/              ← stub-хедеры NXP SDK (если нужны)

CMakePresets.json              just/build.just
  host-debug-build               test-host
  └── targets: [test_<n>]        cmake --build + ctest
```

---

## Шаг 0 — Определить категорию теста

Перед написанием кода определи к какой категории относится модуль:

| Категория | Описание | Инструментарий |
|-----------|----------|----------------|
| **A** | Нет вызовов NXP SDK: алгоритмы, парсеры, FSM, структуры данных | Unity |
| **B** | BSP-модуль вызывает `fsl_*.h`, USB-стек и т.д. | Unity + fff + stub-хедеры |

**Признак категории A:** в `.c` файле модуля нет ни одного `#include "fsl_*.h"`.
**Признак категории B:** есть хотя бы один такой include.

---

## Шаг 1 — Создать тестовый файл

### Категория A — платформонезависимый модуль

```c
/* tests/host/<n>/test_<n>.c */
#include "unity.h"
#include "<n>.h"   /* тестируемый модуль */

void setUp(void)    { /* сброс состояния перед каждым тестом */ }
void tearDown(void) { /* очистка после каждого теста */        }

void test_something(void)
{
    /* Arrange */
    int input = 42;

    /* Act */
    int result = module_process(input);

    /* Assert */
    TEST_ASSERT_EQUAL(expected, result);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_something);
    return UNITY_END();
}
```

### Категория B — BSP-модуль с fff-фейками

Порядок `#include` принципиален — нарушение порядка вызовет ошибки компиляции:

```c
/* tests/host/<n>/test_<n>.c */
#include "unity.h"
#include "fff.h"

DEFINE_FFF_GLOBALS;                /* 1. ровно один раз на весь .c файл */

#include "fsl_<driver>.h"          /* 2. stub-хедер с типами (из mocks/) */

/* 3. объявить фейки для всех SDK-функций, которые вызывает тестируемый модуль */
FAKE_VOID_FUNC(SDK_Function_A, ArgType1, ArgType2);
FAKE_VALUE_FUNC(status_t, SDK_Function_B, ArgType1);

#include "bsp/<module>.h"          /* 4. тестируемый модуль — всегда последним */

void setUp(void)
{
    RESET_FAKE(SDK_Function_A);
    RESET_FAKE(SDK_Function_B);
    FFF_RESET_HISTORY();
    /* при необходимости задать дефолтные return_val */
}

void tearDown(void) { }

void test_something(void)
{
    /* Arrange: настроить поведение фейков */
    SDK_Function_B_fake.return_val = kStatus_Success;

    /* Act */
    bsp_status_t status = bsp_module_do_something();

    /* Assert: проверить результат и вызовы */
    TEST_ASSERT_EQUAL(BSP_OK, status);
    TEST_ASSERT_EQUAL(1, SDK_Function_A_fake.call_count);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_something);
    return UNITY_END();
}
```

---

## Шаг 2 — Stub-хедеры (только категория B)

Если тестируемый модуль использует NXP SDK хедеры которых ещё нет в `tests/host/mocks/` — нужно создать stub.

### Что такое stub-хедер и зачем он нужен

NXP SDK хедеры (`fsl_gpio.h` и др.) тянут платформенные регистровые определения для Cortex-M7 — они не компилируются на хосте. Stub-хедер в `tests/host/mocks/` содержит только минимально необходимые типы и сигнатуры функций. CMake подключает `mocks/` **до** SDK, поэтому компилятор находит stub раньше оригинала.

### Шаблон stub-хедера

Добавляй в stub только то, что реально используется в тестируемом `.c` файле:

```c
/* tests/host/mocks/fsl_<driver>.h */
#pragma once
#include <stdint.h>

/* Минимально необходимые типы */
typedef struct { uint32_t reserved[64]; } DRIVER_Type;

typedef enum {
    kStatus_Success = 0,
    kStatus_Fail    = 1,
} status_t;

/* Сигнатуры функций — реализации предоставляет fff */
void     SDK_Function_A(DRIVER_Type *base, uint32_t arg);
status_t SDK_Function_B(DRIVER_Type *base, const uint8_t *data, size_t len);
```

### Уже существующие stubs в `tests/host/mocks/`

| Файл | Что заменяет | Используется в |
|------|-------------|----------------|
| `fsl_gpio.h` | GPIO драйвер | `test_bsp_led` |
| `pin_mux.h` | Макросы пинов из `generated/` | `test_bsp_led` |
| `board.h` | `board_hw_init()` | `test_bsp_led` |

Если нужный stub уже есть — ничего создавать не нужно, просто укажи `mocks/` в `MOCKS` аргументе `add_host_test()`.

---

## Шаг 3 — Зарегистрировать тест в `tests/host/CMakeLists.txt`

Используй функцию `add_host_test()`. Она создаёт исполняемый файл и регистрирует его в CTest:

```cmake
# Категория A — без mocks
add_host_test(
    NAME    test_<n>
    SOURCES <n>/test_<n>.c
            ${PROJECT_SOURCE_DIR}/<path_to_module>/<module>.c
    INCLUDES
            ${PROJECT_SOURCE_DIR}/<path_to_module>/include
)

# Категория B — с mocks
add_host_test(
    NAME    test_<n>
    SOURCES <n>/test_<n>.c
            ${PROJECT_SOURCE_DIR}/bsp/<module>/src/<module>.c
    INCLUDES
            ${PROJECT_SOURCE_DIR}/bsp/<module>/include
    MOCKS
            ${BSP_MOCKS_DIR}   # = tests/host/mocks/
)
```

### Аргументы `add_host_test()`

| Аргумент | Обязателен | Описание |
|----------|-----------|----------|
| `NAME` | ✓ | Имя исполняемого файла и теста в CTest |
| `SOURCES` | ✓ | Тестовый `.c` + исходники тестируемых модулей |
| `INCLUDES` | — | Дополнительные include-пути (для `#include "bsp/led.h"` и т.д.) |
| `MOCKS` | — | Директории со stub-хедерами (подключаются с высшим приоритетом) |

`lib_external` (Unity + fff) подключается автоматически — добавлять не нужно.

---

## Шаг 4 — Добавить таргет в `CMakePresets.json`

```json
{
    "name": "host-debug-build",
    "configurePreset": "host-debug",
    "targets": [
        "test_bsp_led",
        "test_ring_buffer",
        "test_timeout_pattern",
        "uart_host_mock_example",
        "test_<n>"
    ]
}
```

То же самое для `host-release-build` если нужен Release-прогон.

---

## Шаг 5 — Запустить

```bash
# Сборка + все тесты одной командой (devcontainer)
just build::test-host

# Только новый тест
ctest --preset host-debug-test -R test_<n> -V

# Напрямую — виден полный вывод Unity без CTest-обёртки
./build/host-debug/tests/host/test_<n>
```

---

## Справочник: Unity assertions

```c
/* Целые числа */
TEST_ASSERT_EQUAL(expected, actual)
TEST_ASSERT_EQUAL_INT8 / INT16 / INT32 / UINT8 / UINT32(expected, actual)
TEST_ASSERT_NOT_EQUAL(expected, actual)
TEST_ASSERT_INT_WITHIN(delta, expected, actual)

/* Булевые / указатели */
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)
TEST_ASSERT_NULL(pointer)
TEST_ASSERT_NOT_NULL(pointer)

/* Строки / память */
TEST_ASSERT_EQUAL_STRING(expected, actual)
TEST_ASSERT_EQUAL_MEMORY(expected, actual, len)
TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, len)

/* Явный провал / пропуск */
TEST_FAIL_MESSAGE("причина")
TEST_IGNORE_MESSAGE("в процессе")
```

---

## Справочник: fff-фейки

### Объявление

```c
FAKE_VOID_FUNC(func, ArgType1, ArgType2);          /* void-функция */
FAKE_VALUE_FUNC(RetType, func, ArgType1, ArgType2);/* с возвращаемым значением */
FAKE_VOID_FUNC_VARARG(func, const char *, ...);    /* variadic */
```

### Управление поведением

```c
/* Фиксированное возвращаемое значение */
func_fake.return_val = kStatus_Fail;

/* Последовательность значений */
status_t seq[] = {kStatus_Success, kStatus_Success, kStatus_Fail};
SET_RETURN_SEQ(func, seq, 3);

/* Кастомная реализация — высший приоритет, перекрывает return_val */
func_fake.custom_fake = my_impl;
```

### Проверка вызовов

```c
TEST_ASSERT_EQUAL(2, func_fake.call_count);         /* сколько раз вызвана */
TEST_ASSERT_EQUAL(expected, func_fake.arg0_val);    /* аргумент последнего вызова */
TEST_ASSERT_EQUAL_PTR(func, fff.call_history[0]);   /* порядок вызовов */
TEST_ASSERT_EQUAL(0, func_fake.call_count);         /* не была вызвана */
```

### Сброс в setUp

```c
void setUp(void)
{
    RESET_FAKE(func_a);   /* сбрасывает счётчик, историю, return_val, custom_fake */
    RESET_FAKE(func_b);
    FFF_RESET_HISTORY();  /* сбрасывает глобальную историю порядка вызовов */
}
```

---

## Ловушки

**Dangling pointer из `arg_history[]`.**
`arg_history[]` хранит указатели, не копии. Если функция получала указатель на стековую переменную — после возврата это UB. Использовать `custom_fake` с копированием по значению:

```c
static gpio_pin_config_t s_captured;

static void capture(GPIO_Type *base, uint32_t pin, const gpio_pin_config_t *cfg)
{
    s_captured = *cfg;  /* копия по значению пока стек ещё жив */
}

void setUp(void) {
    RESET_FAKE(GPIO_PinInit);
    GPIO_PinInit_fake.custom_fake = capture;
    bsp_led_init();
}

void test_init_output(void) {
    TEST_ASSERT_EQUAL(kGPIO_DigitalOutput, s_captured.direction);
}
```

**`static` функции не мокаются.**
FFF не видит `static` функции снаружи translation unit. Решение — compile-time seam:

```c
#ifdef UNIT_TEST
void internal_fn(void);   /* тест подставит свою реализацию */
#else
static void internal_fn(void) { ... }
#endif
```

**Отсутствие стандартных хедеров в BSP.**
BSP-модули должны явно включать `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` — не полагаться на транзитивное подтягивание через NXP SDK. На хосте этот транзит отсутствует, компиляция упадёт с `undeclared identifier 'size_t'`.

---

## Полный цикл

```bash
# 1. Создать тестовый файл
tests/host/<n>/test_<n>.c

# 2. Создать stub-хедер (если категория B и stub не существует)
tests/host/mocks/fsl_<driver>.h

# 3. Добавить вызов add_host_test() в
tests/host/CMakeLists.txt

# 4. Добавить "test_<n>" в targets в
CMakePresets.json   ← host-debug-build и host-release-build

# 5. Запустить в devcontainer
just build::test-host
```

---

## Чеклист

```bash
[ ] Определена категория (A или B)
[ ] tests/host/<n>/test_<n>.c           — тест с main(), setUp(), tearDown()
[ ] tests/host/mocks/fsl_<driver>.h     — stub (только категория B, если нет)
[ ] tests/host/CMakeLists.txt           — add_host_test(NAME test_<n> ...)
[ ] CMakePresets.json                   — добавить test_<n> в host-debug-build
[ ] just build::test-host               — зелёный прогон
```

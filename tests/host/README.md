# Host unit-тесты: Unity + FFF

## Содержание

- [1. Две категории тестируемых модулей](#1-две-категории-тестируемых-модулей)
- [2. Структура директорий](#2-структура-директорий)
- [3. Структура тестового файла](#3-структура-тестового-файла)
- [4. Unity — assertion API](#4-unity--assertion-api)
- [5. FFF — создание фейков](#5-fff--создание-фейков)
- [6. FFF — управление поведением](#6-fff--управление-поведением)
- [7. FFF — проверка вызовов](#7-fff--проверка-вызовов)
- [8. Работа со stub-хедерами NXP SDK](#8-работа-со-stub-хедерами-nxp-sdk)
- [9. Ловушки и обходные пути](#9-ловушки-и-обходные-пути)
- [10. setUp / tearDown — правильный сброс](#10-setup--teardown--правильный-сброс)
- [11. CMakeLists.txt для host-тестов](#11-cmakeliststxt-для-host-тестов)
- [12. Запуск тестов](#12-запуск-тестов)

---

## 1. Две категории тестируемых модулей

Прежде чем писать тест — определи к какой категории относится модуль. От этого зависит какой инструментарий нужен.

### Категория A — платформонезависимые модули

Модули без единого вызова NXP SDK: парсеры, протоколы, конечные автоматы, алгоритмы, структуры данных. Зависят только от стандартной библиотеки C.

**Инструментарий: только Unity.**

```bash
тест (Unity assertions)
        ↓
тестируемый модуль (protocol.c, fsm.c, ...)
        ↓
stdlib (string.h, stdint.h, ...)   ← всё доступно на хосте нативно
```

Пример: тест JSON-протокола `firmware_test`, тест логики Test runner, тест FSM.

```c
/* tests/host/test_protocol.c */
#include "unity.h"
#include "protocol.h"   /* платформонезависимый модуль */

void test_parse_run_all_command(void) {
    proto_cmd_t cmd;
    int r = proto_parse("{\"type\":\"cmd\",\"cmd\":\"run_all\"}\n", &cmd);
    TEST_ASSERT_EQUAL(0, r);
    TEST_ASSERT_EQUAL(CMD_RUN_ALL, cmd.type);
}
```

### Категория B — BSP-модули (привязка к NXP SDK)

Модули из `bsp/` которые вызывают `fsl_gpio.h`, `fsl_lpuart.h`, NXP USB stack и т.д. На хосте этих хедеров нет — нужны stub-хедеры и fff-фейки.

**Инструментарий: Unity + fff + stub-хедеры.**

```bash
тест (Unity assertions)
        ↓
тестируемый модуль (bsp/led/src/led.c)
        ↓
fff-фейки (FAKE_VOID_FUNC, FAKE_VALUE_FUNC)  ← подменяют NXP SDK функции
        ↓
stub-хедеры (tests/host/mocks/fsl_gpio.h)    ← подменяют NXP SDK хедеры
```

Принцип **seam (шов)**: тестируемый код не знает что вызывает фейк — линковщик и include path подставляют нужную реализацию в зависимости от сборки.

---

## 2. Структура директорий

```bash
tests/host/
├── CMakeLists.txt
├── mocks/                        # stub-хедеры, заменяющие NXP SDK на хосте
│   ├── fsl_gpio.h                # минимальные типы + сигнатуры GPIO
│   ├── fsl_lpuart.h
│   ├── pin_mux.h                 # зеркало макросов пинов из generated/
│   └── board.h
├── led/test_led.c                    # категория B — BSP-модуль
├── protocol/test_protocol.c               # категория A — платформонезависимый
└── runner/test_runner_logic.c           # категория A
```

`mocks/` подключается как include path с более высоким приоритетом чем `sdk/`. Компилятор найдёт `fsl_gpio.h` из `mocks/` раньше чем из SDK — `led.c` компилируется на хосте без единого изменения в BSP-коде.

---

## 3. Структура тестового файла

### Категория A — без фейков

```c
#include "unity.h"
#include "protocol.h"   /* тестируемый модуль */

void setUp(void)    { /* сброс состояния если нужен */ }
void tearDown(void) { }

void test_something(void) {
    TEST_ASSERT_EQUAL(expected, actual);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_something);
    return UNITY_END();
}
```

### Категория B — с fff-фейками

```c
#include "unity.h"
#include "fff.h"

DEFINE_FFF_GLOBALS;  /* ровно один раз на весь .c файл */

/* 1. Подключаем stub-хедер с типами */
#include "fsl_gpio.h"

/* 2. Объявляем фейки для функций которые вызывает тестируемый модуль */
FAKE_VOID_FUNC(GPIO_PinInit,  GPIO_Type *, uint32_t, const gpio_pin_config_t *);
FAKE_VOID_FUNC(GPIO_PinWrite, GPIO_Type *, uint32_t, uint8_t);

/* 3. Подключаем тестируемый модуль — ПОСЛЕ фейков */
#include "bsp/led.h"

void setUp(void) {
    RESET_FAKE(GPIO_PinInit);
    RESET_FAKE(GPIO_PinWrite);
    FFF_RESET_HISTORY();
    led_init();
}

void tearDown(void) { }

void test_led_on_writes_gpio_low(void) {
    led_on(LED_HEARTBEAT);
    TEST_ASSERT_EQUAL_UINT8(0U, GPIO_PinWrite_fake.arg2_val); /* active LOW */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_led_on_writes_gpio_low);
    return UNITY_END();
}
```

---

## 4. Unity — assertion API

### Целые числа

```c
TEST_ASSERT_EQUAL(expected, actual)
TEST_ASSERT_EQUAL_INT(expected, actual)
TEST_ASSERT_EQUAL_INT8(expected, actual)
TEST_ASSERT_EQUAL_INT16(expected, actual)
TEST_ASSERT_EQUAL_INT32(expected, actual)
TEST_ASSERT_EQUAL_UINT8(expected, actual)
TEST_ASSERT_EQUAL_UINT32(expected, actual)
TEST_ASSERT_NOT_EQUAL(expected, actual)
```

### Числа с плавающей точкой

```c
TEST_ASSERT_EQUAL_FLOAT(expected, actual)
TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)  /* |actual - expected| < delta */
TEST_ASSERT_EQUAL_DOUBLE(expected, actual)
```

### Булевые значения

```c
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)
TEST_ASSERT_NULL(pointer)
TEST_ASSERT_NOT_NULL(pointer)
```

### Указатели

```c
TEST_ASSERT_EQUAL_PTR(expected, actual)
```

### Строки и массивы

```c
TEST_ASSERT_EQUAL_STRING(expected, actual)
TEST_ASSERT_EQUAL_MEMORY(expected, actual, len)
TEST_ASSERT_EQUAL_INT_ARRAY(expected, actual, len)
TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, len)
```

### Диапазоны

```c
TEST_ASSERT_INT_WITHIN(delta, expected, actual)
TEST_ASSERT_UINT32_WITHIN(delta, expected, actual)
```

### Явный провал / пропуск

```c
TEST_FAIL()
TEST_FAIL_MESSAGE("причина")
TEST_IGNORE()
TEST_IGNORE_MESSAGE("в процессе")
```

---

## 5. FFF — создание фейков

### Макросы объявления

```c
/* void-функция без аргументов */
FAKE_VOID_FUNC(HAL_Init);

/* void-функция с аргументами */
FAKE_VOID_FUNC(GPIO_PinInit, GPIO_Type *, uint32_t, const gpio_pin_config_t *);

/* функция с возвращаемым значением */
FAKE_VALUE_FUNC(status_t, LPUART_WriteBlocking, LPUART_Type *, const uint8_t *, size_t);

/* без аргументов с возвращаемым значением */
FAKE_VALUE_FUNC(uint32_t, get_tick_ms);

/* переменное число аргументов */
FAKE_VOID_FUNC_VARARG(debug_printf, const char *, ...);
```

### Расположение фейков в файле

Фейки объявляются **в тестовом .c файле** прямо перед `#include` тестируемого модуля. Для небольших проектов отдельный `fakes.h/fakes.c` избыточен — каждый тестовый файл объявляет только те фейки, которые нужны именно ему.

```c
/* Правильный порядок в тестовом файле */
#include "unity.h"
#include "fff.h"
DEFINE_FFF_GLOBALS;               /* 1. глобальный контекст fff */

#include "fsl_gpio.h"             /* 2. stub-хедер с типами */
FAKE_VOID_FUNC(GPIO_PinWrite, GPIO_Type *, uint32_t, uint8_t); /* 3. фейк */

#include "bsp/led.h"              /* 4. тестируемый модуль — последним */
```

---

## 6. FFF — управление поведением

### Задать возвращаемое значение

```c
/* константа — при каждом вызове */
LPUART_WriteBlocking_fake.return_val = kStatus_Fail;

/* последовательность — каждый вызов берёт следующее */
status_t seq[] = {kStatus_Success, kStatus_Success, kStatus_Timeout};
SET_RETURN_SEQ(LPUART_WriteBlocking, seq, 3);
/* 1-й вызов → kStatus_Success */
/* 2-й вызов → kStatus_Success */
/* 3-й вызов → kStatus_Timeout */
/* 4-й и далее → последнее (kStatus_Timeout) */
```

### custom_fake — кастомная реализация

```c
/* эмуляция тикающего таймера */
static uint32_t s_tick = 0;
static uint32_t fake_tick_inc(void) { s_tick += 10; return s_tick; }

void test_timeout_fires_after_100ms(void) {
    get_tick_ms_fake.custom_fake = fake_tick_inc;
    s_tick = 0;

    bool result = wait_with_timeout(100);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(11, get_tick_ms_fake.call_count);
}
```

> `custom_fake` имеет наивысший приоритет — если задан, `return_val` и
> `return_val_seq` игнорируются.

### custom_fake — захват аргументов по значению

Используй когда нужно проверить содержимое структуры, переданной по указателю.
**Не используй `arg_history[]` для указателей на стековые переменные** — после возврата из тестируемой функции это dangling pointer (ASAN: `stack-use-after-return`).

```c
/* НЕПРАВИЛЬНО — cfg живёт на стеке led_init(), после return — dangling */
led_init();
const gpio_pin_config_t *cfg = GPIO_PinInit_fake.arg2_history[0]; /* UB! */
TEST_ASSERT_EQUAL(kGPIO_DigitalOutput, cfg->direction);

/* ПРАВИЛЬНО — копируем по значению пока стек ещё жив */
static gpio_pin_config_t s_captured[2];
static int s_idx = 0;

static void capture_cfg(GPIO_Type *base, uint32_t pin,
                        const gpio_pin_config_t *cfg) {
    (void)base; (void)pin;
    if (s_idx < 2) s_captured[s_idx++] = *cfg;  /* копия по значению */
}

void setUp(void) {
    RESET_FAKE(GPIO_PinInit);
    s_idx = 0;
    GPIO_PinInit_fake.custom_fake = capture_cfg;  /* подключить ДО вызова */
    led_init();
}

void test_init_configures_as_output(void) {
    TEST_ASSERT_EQUAL(kGPIO_DigitalOutput, s_captured[0].direction);
}
```

---

## 7. FFF — проверка вызовов

### Счётчик и аргументы последнего вызова

```c
TEST_ASSERT_EQUAL(2, GPIO_PinWrite_fake.call_count);
TEST_ASSERT_EQUAL_UINT8(0U, GPIO_PinWrite_fake.arg2_val);  /* последний вызов */
```

### История всех вызовов

```c
/* arg_history хранит FFF_ARG_HISTORY_LEN (по умолчанию 50) последних вызовов */
/* Используй только для скалярных типов и не-стековых указателей */
TEST_ASSERT_EQUAL_UINT8(0U, GPIO_PinWrite_fake.arg2_history[0]);
TEST_ASSERT_EQUAL_UINT8(1U, GPIO_PinWrite_fake.arg2_history[1]);
```

### Порядок вызовов разных функций

```c
void test_init_sequence_order(void) {
    board_init();
    /* clock_init должен вызваться раньше gpio_init */
    TEST_ASSERT_EQUAL_PTR(clock_init, fff.call_history[0]);
    TEST_ASSERT_EQUAL_PTR(gpio_init,  fff.call_history[1]);
}
```

### Функция не была вызвана

```c
void test_no_gpio_write_on_error(void) {
    LPUART_WriteBlocking_fake.return_val = kStatus_Fail;
    module_process();
    TEST_ASSERT_EQUAL(0, GPIO_PinWrite_fake.call_count);
}
```

---

## 8. Работа со stub-хедерами NXP SDK

### Зачем нужны stub-хедеры

NXP SDK хедеры (`fsl_gpio.h`, `fsl_lpuart.h` и т.д.) не компилируются на хосте — они тянут платформенные регистровые определения для Cortex-M7. Stub-хедер в `tests/host/mocks/` содержит только минимально необходимые типы и сигнатуры функций.

### Как stub-хедер «перекрывает» SDK

В CMakeLists для тестового таргета `mocks/` добавляется в include path **до** SDK:

```cmake
target_include_directories(test_led PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/host/mocks   # ← ищется первым
    ${CMAKE_SOURCE_DIR}/bsp/led/include
)
```

Компилятор найдёт `fsl_gpio.h` из `mocks/` раньше чем из `sdk/` — `led.c` компилируется без изменений.

### Что должно быть в stub-хедере

Только то, что реально используется в тестируемом `.c` файле. Не копировать весь SDK хедер.

```c
/* tests/host/mocks/fsl_gpio.h */
#pragma once
#include <stdint.h>

typedef struct { uint32_t reserved[64]; } GPIO_Type;

typedef enum { kGPIO_DigitalInput = 0U, kGPIO_DigitalOutput = 1U } gpio_pin_direction_t;
typedef enum { kGPIO_NoIntmode = 0U } gpio_interrupt_mode_t;

typedef struct {
    gpio_pin_direction_t  direction;
    uint8_t               outputLogic;
    gpio_interrupt_mode_t interruptMode;
} gpio_pin_config_t;

/* сигнатуры — реализации предоставляет fff */
void GPIO_PinInit(GPIO_Type *base, uint32_t pin, const gpio_pin_config_t *config);
void GPIO_PinWrite(GPIO_Type *base, uint32_t pin, uint8_t output);
```

### stub pin_mux.h — зеркало макросов пинов

`pin_mux.h` из `generated/` тоже недоступен на хосте. Создаём stub который зеркалит реальные значения:

```c
/* tests/host/mocks/pin_mux.h */
#pragma once
#include "fsl_gpio.h"

static GPIO_Type stub_GPIO3;

#define BOARD_INITPINS_UserLed1_GPIO      (&stub_GPIO3)
#define BOARD_INITPINS_UserLed1_GPIO_PIN  3U
#define BOARD_INITPINS_UserLed2_GPIO      (&stub_GPIO3)
#define BOARD_INITPINS_UserLed2_GPIO_PIN  4U
```

> При изменении пинов в `generated/pin_mux.h` — обновить соответствующий stub вручную.

---

## 9. Ловушки и обходные пути

### `static` функции

FFF не может замокать `static` функции — они невидимы снаружи translation unit.

```c
/* ❌ нельзя замокать напрямую */
static void internal_process(void) { ... }

/* ✅ compile-time seam */
#ifdef UNIT_TEST
    void internal_process(void);   /* тест подставит свою реализацию */
#else
    static void internal_process(void) { ... }
#endif
```

### Dangling pointer из arg_history

`arg_history[]` хранит указатели — **не** копии. Для структур передаваемых по указателю из функций с коротким временем жизни (локальные переменные) использовать `custom_fake` с копированием по значению. Подробнее — в разделе 6.

### Настройка лимитов истории

```c
/* переопределить перед включением fff.h */
#define FFF_ARG_HISTORY_LEN  100
#define FFF_CALL_HISTORY_LEN 100
#include "fff.h"
```

### Стандартные хедеры в BSP

BSP-модули должны явно включать `<stddef.h>`, `<stdint.h>`, `<stdbool.h>` — не полагаться на транзитивное подтягивание через NXP SDK. На хосте этот транзит отсутствует и компиляция упадёт с `undeclared identifier 'size_t'`.

---

## 10. setUp / tearDown — правильный сброс

`RESET_FAKE` сбрасывает для одного фейка: счётчик вызовов, историю аргументов, `return_val`, `custom_fake`.

`FFF_RESET_HISTORY` сбрасывает глобальную историю порядка вызовов.

```c
void setUp(void)
{
    RESET_FAKE(GPIO_PinInit);
    RESET_FAKE(GPIO_PinWrite);
    FFF_RESET_HISTORY();

    /* восстановить дефолтное поведение если нужно */
    LPUART_WriteBlocking_fake.return_val = kStatus_Success;
}
```

> Никогда не полагайся на порядок выполнения тестов. Каждый тест должен
> работать независимо — `setUp` обязан полностью сбрасывать состояние.

---

## 11. CMakeLists.txt для host-тестов

```cmake
# tests/host/CMakeLists.txt

function(add_host_test)
  cmake_parse_arguments(ARG "" "NAME" "SOURCES;MOCKS" ${ARGN})

  add_executable(${ARG_NAME} ${ARG_SOURCES})

  target_link_libraries(${ARG_NAME} PRIVATE lib_external)

  # mocks/ подключается первым — перекрывает SDK хедеры
  foreach(mock_dir IN LISTS ARG_MOCKS)
    target_include_directories(${ARG_NAME} PRIVATE ${mock_dir})
  endforeach()

  add_test(
    NAME    ${ARG_NAME}
    COMMAND ${ARG_NAME}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endfunction()

set(MOCKS ${CMAKE_CURRENT_SOURCE_DIR}/mocks)

# категория A — платформонезависимый, без mocks
add_host_test(
  NAME    test_protocol
  SOURCES test_protocol.c
          ${PROJECT_SOURCE_DIR}/firmware/test/src/protocol.c
)

# категория B — BSP-модуль, нужны mocks
add_host_test(
  NAME    test_led
  SOURCES test_led.c
          ${PROJECT_SOURCE_DIR}/bsp/led/src/led.c
  MOCKS   ${MOCKS}
)
```

---

## 12. Запуск тестов

```bash
# конфигурация (один раз или после изменения CMakeLists)
cmake --preset host-debug

# сборка + тесты одной командой
just build::test-host

# или по шагам:
cmake --build --preset host-debug-build
ctest --preset host-debug-test

# конкретный тест с полным выводом Unity
ctest --preset host-debug-test -R test_led -V

# напрямую — видно весь вывод без CTest-обёртки
./build/host-debug/tests/host/test_led
```

### Пример вывода при успехе

```bash
test_led.c:58:test_led_init_calls_gpio_init_for_each_led:PASS
test_led.c:64:test_led_init_configures_as_output:PASS
test_led.c:71:test_led_init_output_logic_is_high:PASS
test_led.c:77:test_led_on_writes_gpio_low:PASS

-----------------------
14 Tests 0 Failures 0 Ignored
OK
```

### Пример вывода при провале

```bash
test_led.c:78:test_led_on_writes_gpio_low:FAIL:
  Expected 0 Was 1

-----------------------
14 Tests 1 Failures 0 Ignored
FAIL
```

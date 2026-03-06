# Юнит тестирование: Unity + FFF

## Содержание

- [Юнит тестирование: Unity + FFF](#юнит-тестирование-unity--fff)
  - [Содержание](#содержание)
  - [1. Архитектура тестирования](#1-архитектура-тестирования)
  - [2. Структура тестового файла](#2-структура-тестового-файла)
  - [3. Unity — assertion API](#3-unity--assertion-api)
    - [Целые числа](#целые-числа)
    - [Числа с плавающей точкой](#числа-с-плавающей-точкой)
    - [Булевые значения](#булевые-значения)
    - [Указатели](#указатели)
    - [Строки и массивы](#строки-и-массивы)
    - [Диапазоны](#диапазоны)
    - [Явный провал / пропуск](#явный-провал--пропуск)
  - [4. FFF — создание фейков](#4-fff--создание-фейков)
    - [Макросы объявления](#макросы-объявления)
    - [Где объявлять](#где-объявлять)
  - [5. FFF — управление поведением](#5-fff--управление-поведением)
    - [Задать возвращаемое значение](#задать-возвращаемое-значение)
    - [Подставить кастомную реализацию](#подставить-кастомную-реализацию)
    - [Захват аргументов через custom\_fake](#захват-аргументов-через-custom_fake)
  - [6. FFF — проверка вызовов](#6-fff--проверка-вызовов)
    - [Счётчики и аргументы последнего вызова](#счётчики-и-аргументы-последнего-вызова)
    - [История всех вызовов](#история-всех-вызовов)
    - [Порядок вызовов разных функций](#порядок-вызовов-разных-функций)
    - [Функция не была вызвана](#функция-не-была-вызвана)
  - [7. Паттерны работы с HAL](#7-паттерны-работы-с-hal)
    - [Паттерн: заглушка HAL для host-сборки](#паттерн-заглушка-hal-для-host-сборки)
    - [Паттерн: мок таймера](#паттерн-мок-таймера)
    - [Паттерн: тест конечного автомата (FSM)](#паттерн-тест-конечного-автомата-fsm)
  - [8. Организация фейков в проекте](#8-организация-фейков-в-проекте)
  - [9. setUp / tearDown — правильный сброс](#9-setup--teardown--правильный-сброс)
  - [10. Ограничения и обходные пути](#10-ограничения-и-обходные-пути)
    - [`static` функции](#static-функции)
    - [Настройка лимитов истории](#настройка-лимитов-истории)
    - [Глобальные хендлеры HAL](#глобальные-хендлеры-hal)
  - [11. CMakeLists.txt для host-тестов](#11-cmakeliststxt-для-host-тестов)
  - [12. Запуск тестов](#12-запуск-тестов)
    - [Пример вывода при успехе](#пример-вывода-при-успехе)
    - [Пример вывода при провале](#пример-вывода-при-провале)

---

## 1. Архитектура тестирования

В bare-metal проекте тестируемый код вызывает HAL-функции, которые недоступны на хосте.
FFF подменяет эти вызовы фейками, Unity проверяет результаты.

```
┌─────────────────────────────────────────────┐
│              Host Test Runner               │
│                                             │
│  ┌──────────┐    ┌──────────┐               │
│  │  Unity   │    │   FFF    │               │
│  │ (assert) │    │ (fakes)  │               │
│  └──────────┘    └──────────┘               │
│        ↓               ↓                   │
│  ┌─────────────────────────────┐            │
│  │     Тестируемый код         │            │
│  │     App/Src/led.c           │            │
│  └─────────────────────────────┘            │
│        ↓ вызывает                           │
│  ┌─────────────────────────────┐            │
│  │  HAL Fakes (вместо реального│            │
│  │  stm32f4xx_hal.h)           │            │
│  └─────────────────────────────┘            │
└─────────────────────────────────────────────┘
```

**Принцип seam (шов):** тестируемый код не знает, что вызывает фейк, а не реальный HAL — линковщик подставляет нужную реализацию в зависимости от сборки.

---

## 2. Структура тестового файла

```c
// Tests/host/test_led.c

#include "fff.h"         // мок-фреймворк
#include "unity.h"       // assertion фреймворк
#include "hal_fakes.h"   // объявления фейков HAL
#include "led.h"         // тестируемый модуль

// Обязательно — ровно один раз на весь тестовый файл
DEFINE_FFF_GLOBALS;

// ─── Жизненный цикл теста ─────────────────────────────────────────────────

void setUp(void)
{
    // Сбрасываем фейки перед каждым тестом
    RESET_FAKE(HAL_GPIO_WritePin);
    RESET_FAKE(HAL_GetTick);
    FFF_RESET_HISTORY();
}

void tearDown(void) {}

// ─── Тесты ────────────────────────────────────────────────────────────────

void test_led_on_sets_gpio_high(void)
{
    led_set(LED_ON);

    TEST_ASSERT_EQUAL(1, HAL_GPIO_WritePin_fake.call_count);
    TEST_ASSERT_EQUAL(GPIO_PIN_SET, HAL_GPIO_WritePin_fake.arg2_val);
}

void test_led_off_sets_gpio_low(void)
{
    led_set(LED_OFF);

    TEST_ASSERT_EQUAL(GPIO_PIN_RESET, HAL_GPIO_WritePin_fake.arg2_val);
}

// ─── Runner ───────────────────────────────────────────────────────────────

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_led_on_sets_gpio_high);
    RUN_TEST(test_led_off_sets_gpio_low);
    return UNITY_END();
}
```

---

## 3. Unity — assertion API

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
TEST_ASSERT_FLOAT_WITHIN(delta, expected, actual)  // |actual - expected| < delta
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
TEST_ASSERT_EQUAL_MEMORY(expected, actual, len)    // побайтовое сравнение
TEST_ASSERT_EQUAL_INT_ARRAY(expected, actual, len) // сравнение массивов int
TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, actual, len)
```

### Диапазоны

```c
TEST_ASSERT_INT_WITHIN(delta, expected, actual)
TEST_ASSERT_UINT32_WITHIN(delta, expected, actual)
```

### Явный провал / пропуск

```c
TEST_FAIL()                        // тест падает безусловно
TEST_FAIL_MESSAGE("причина")       // с сообщением
TEST_IGNORE()                      // тест пропускается (помечается как ignored)
TEST_IGNORE_MESSAGE("в процессе")
```

---

## 4. FFF — создание фейков

### Макросы объявления

```c
// void функция без аргументов
FAKE_VOID_FUNC(HAL_Init);

// void функция с аргументами
FAKE_VOID_FUNC(HAL_GPIO_WritePin,
               GPIO_TypeDef*,   // arg0
               uint16_t,        // arg1
               GPIO_PinState);  // arg2

// Функция с возвращаемым значением
FAKE_VALUE_FUNC(HAL_StatusTypeDef,
                HAL_UART_Transmit,
                UART_HandleTypeDef*,  // arg0
                uint8_t*,             // arg1
                uint16_t,             // arg2
                uint32_t);            // arg3

// Без аргументов, с возвращаемым значением
FAKE_VALUE_FUNC(uint32_t, HAL_GetTick);

// Функция с переменным числом аргументов
FAKE_VOID_FUNC_VARARG(printf_fake, const char*, ...);
```

### Где объявлять

| Место | Назначение |
|---|---|
| `hal_fakes.h` | `DECLARE_FAKE_*` — объявления для хедера |
| `hal_fakes.c` | `DEFINE_FAKE_*` — определения, компилируется один раз |
| Тестовый файл | `DEFINE_FFF_GLOBALS` — ровно в одном `.c` файле |

```c
// hal_fakes.h
#pragma once
#include "fff.h"
#include "stm32f4xx_hal.h"   // типы GPIO_TypeDef и т.д.

DECLARE_FAKE_VOID_FUNC(HAL_GPIO_WritePin, GPIO_TypeDef*, uint16_t, GPIO_PinState);
DECLARE_FAKE_VALUE_FUNC(uint32_t, HAL_GetTick);
DECLARE_FAKE_VALUE_FUNC(HAL_StatusTypeDef, HAL_UART_Transmit,
                        UART_HandleTypeDef*, uint8_t*, uint16_t, uint32_t);
```

```c
// hal_fakes.c
#include "hal_fakes.h"

DEFINE_FAKE_VOID_FUNC(HAL_GPIO_WritePin, GPIO_TypeDef*, uint16_t, GPIO_PinState);
DEFINE_FAKE_VALUE_FUNC(uint32_t, HAL_GetTick);
DEFINE_FAKE_VALUE_FUNC(HAL_StatusTypeDef, HAL_UART_Transmit,
                       UART_HandleTypeDef*, uint8_t*, uint16_t, uint32_t);
```

---

## 5. FFF — управление поведением

### Задать возвращаемое значение

```c
// Константное значение — возвращается при каждом вызове
HAL_UART_Transmit_fake.return_val = HAL_ERROR;

// Последовательность значений — каждый вызов берёт следующее
HAL_StatusTypeDef seq[] = {HAL_OK, HAL_OK, HAL_TIMEOUT};
SET_RETURN_SEQ(HAL_UART_Transmit, seq, 3);
// 1-й вызов → HAL_OK
// 2-й вызов → HAL_OK
// 3-й вызов → HAL_TIMEOUT
// 4-й и далее → последнее значение (HAL_TIMEOUT)
```

### Подставить кастомную реализацию

```c
// Эмулируем тикающий таймер
static uint32_t s_tick_ms = 0;

static uint32_t fake_get_tick_incrementing(void)
{
    s_tick_ms += 10;
    return s_tick_ms;
}

void test_timeout_after_100ms(void)
{
    HAL_GetTick_fake.custom_fake = fake_get_tick_incrementing;
    s_tick_ms = 0;

    bool result = wait_with_timeout(100);

    TEST_ASSERT_TRUE(result);
    TEST_ASSERT_EQUAL(11, HAL_GetTick_fake.call_count);
}
```

> `custom_fake` имеет наивысший приоритет — если задан, `return_val` и
> `return_val_seq` игнорируются.

### Захват аргументов через custom_fake

```c
static uint8_t  s_captured_data[64];
static uint16_t s_captured_size;

static HAL_StatusTypeDef fake_uart_capture(UART_HandleTypeDef *p_huart,
                                           uint8_t            *p_data,
                                           uint16_t            size,
                                           uint32_t            timeout)
{
    s_captured_size = size;
    memcpy(s_captured_data, p_data, size);
    return HAL_OK;
}

void test_uart_sends_correct_payload(void)
{
    HAL_UART_Transmit_fake.custom_fake = fake_uart_capture;

    protocol_send_ping();

    TEST_ASSERT_EQUAL(4, s_captured_size);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("\xAA\x01\x00\xBB", s_captured_data, 4);
}
```

---

## 6. FFF — проверка вызовов

### Счётчики и аргументы последнего вызова

```c
// Количество вызовов
TEST_ASSERT_EQUAL(2, HAL_GPIO_WritePin_fake.call_count);

// Аргументы последнего вызова
TEST_ASSERT_EQUAL_PTR(GPIOA,       HAL_GPIO_WritePin_fake.arg0_val);
TEST_ASSERT_EQUAL(GPIO_PIN_5,      HAL_GPIO_WritePin_fake.arg1_val);
TEST_ASSERT_EQUAL(GPIO_PIN_SET,    HAL_GPIO_WritePin_fake.arg2_val);
```

### История всех вызовов

```c
// История аргументов — по умолчанию хранит FFF_ARG_HISTORY_LEN=50 вызовов
TEST_ASSERT_EQUAL(GPIO_PIN_SET,   HAL_GPIO_WritePin_fake.arg2_history[0]);
TEST_ASSERT_EQUAL(GPIO_PIN_RESET, HAL_GPIO_WritePin_fake.arg2_history[1]);
```

### Порядок вызовов разных функций

```c
// FFF хранит глобальную историю вызовов всех фейков
void test_init_sequence_order(void)
{
    module_init();

    // HAL_Init должен вызваться раньше HAL_GPIO_Init
    TEST_ASSERT_EQUAL_PTR(HAL_Init,      fff.call_history[0]);
    TEST_ASSERT_EQUAL_PTR(HAL_GPIO_Init, fff.call_history[1]);
}
```

### Функция не была вызвана

```c
void test_no_gpio_toggle_on_error(void)
{
    HAL_UART_Transmit_fake.return_val = HAL_ERROR;

    module_process();

    TEST_ASSERT_EQUAL(0, HAL_GPIO_WritePin_fake.call_count);
}
```

---

## 7. Паттерны работы с HAL

### Паттерн: заглушка HAL для host-сборки

Создай `Tests/host/stubs/stm32f4xx_hal.h` — минимальный хедер с нужными типами:

```c
// Tests/host/stubs/stm32f4xx_hal.h
#pragma once
#include <stdint.h>

typedef struct { uint32_t placeholder; } GPIO_TypeDef;
typedef struct { uint32_t placeholder; } UART_HandleTypeDef;

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;

#define GPIOA ((GPIO_TypeDef *)0x40020000)
#define GPIOB ((GPIO_TypeDef *)0x40020400)

#define GPIO_PIN_5  ((uint16_t)0x0020)
#define GPIO_PIN_13 ((uint16_t)0x2000)
```

### Паттерн: мок таймера

```c
static uint32_t s_mock_tick = 0;

void mock_tick_reset(void)          { s_mock_tick = 0; }
void mock_tick_advance(uint32_t ms) { s_mock_tick += ms; }

static uint32_t fake_hal_get_tick(void) { return s_mock_tick; }

void setUp(void)
{
    RESET_FAKE(HAL_GetTick);
    HAL_GetTick_fake.custom_fake = fake_hal_get_tick;
    mock_tick_reset();
}

void test_retry_stops_after_timeout(void)
{
    HAL_UART_Transmit_fake.return_val = HAL_TIMEOUT;

    bool result = uart_send_with_retry("data", 4, /*timeout_ms=*/300);

    TEST_ASSERT_FALSE(result);
    // За 300ms при retry каждые 100ms — ровно 3 попытки
    TEST_ASSERT_EQUAL(3, HAL_UART_Transmit_fake.call_count);
}
```

### Паттерн: тест конечного автомата (FSM)

```c
void test_fsm_transitions_on_uart_error(void)
{
    HAL_StatusTypeDef seq[] = {HAL_OK, HAL_ERROR};
    SET_RETURN_SEQ(HAL_UART_Transmit, seq, 2);

    fsm_init();
    fsm_step();
    fsm_step();

    TEST_ASSERT_EQUAL(FSM_STATE_ERROR, fsm_get_state());
    TEST_ASSERT_EQUAL(2, HAL_UART_Transmit_fake.call_count);
}
```

---

## 8. Организация фейков в проекте

```
Tests/
├── CMakeLists.txt
├── host/
│   ├── CMakeLists.txt
│   ├── stubs/
│   │   ├── stm32f4xx_hal.h       # минимальные типы для компиляции на хосте
│   │   └── stm32f4xx_hal_conf.h
│   ├── fakes/
│   │   ├── hal_fakes.h           # DECLARE_FAKE_* для всех HAL функций
│   │   └── hal_fakes.c           # DEFINE_FAKE_* — компилируется один раз
│   ├── test_led.c
│   ├── test_uart.c
│   └── test_fsm.c
└── target/
    └── ...
```

---

## 9. setUp / tearDown — правильный сброс

`RESET_FAKE` сбрасывает для одного фейка: счётчик вызовов, историю аргументов, `return_val`, `custom_fake`.

`FFF_RESET_HISTORY` сбрасывает глобальную историю порядка вызовов.

```c
void setUp(void)
{
    RESET_FAKE(HAL_GPIO_WritePin);
    RESET_FAKE(HAL_UART_Transmit);
    RESET_FAKE(HAL_GetTick);

    FFF_RESET_HISTORY();

    // Восстанавливаем дефолтное поведение
    HAL_GetTick_fake.return_val       = 0;
    HAL_UART_Transmit_fake.return_val = HAL_OK;
}
```

> Никогда не полагайся на порядок выполнения тестов. Каждый тест должен
> работать независимо — `setUp` обязан полностью сбрасывать состояние.

---

## 10. Ограничения и обходные пути

### `static` функции

FFF не может замокать `static` функции — они невидимы снаружи translation unit.

```c
// ❌ Нельзя замокать напрямую
static void internal_process(void) { ... }

// ✅ Решение — compile-time seam
#ifdef UNIT_TEST
    void internal_process(void);   // тест подставит свою реализацию
#else
    static void internal_process(void) { ... }
#endif
```

### Настройка лимитов истории

```c
// Переопределяется перед включением fff.h
#define FFF_ARG_HISTORY_LEN  100   // сколько вызовов хранить на фейк
#define FFF_CALL_HISTORY_LEN 100   // размер глобальной истории вызовов
#include "fff.h"
```

### Глобальные хендлеры HAL

```c
// Тестовый файл — заглушка глобального хендлера
UART_HandleTypeDef huart1 = {0};
```

---

## 11. CMakeLists.txt для host-тестов

```cmake
# Tests/host/CMakeLists.txt

# Библиотека фейков — общая для всех тестов
add_library(hal_fakes STATIC fakes/hal_fakes.c)
target_include_directories(hal_fakes PUBLIC fakes/ stubs/)
target_link_libraries(hal_fakes PUBLIC ThirdParty)

# Макрос для регистрации тестов
function(add_host_test TEST_NAME TEST_SOURCES APP_SOURCES)
    add_executable(${TEST_NAME} ${TEST_SOURCES} ${APP_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/App/Inc
    )
    target_compile_definitions(${TEST_NAME} PRIVATE UNIT_TEST)
    target_link_libraries(${TEST_NAME} PRIVATE hal_fakes ThirdParty)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_host_test(test_led
    test_led.c
    ${PROJECT_SOURCE_DIR}/App/Src/led.c
)

add_host_test(test_uart
    test_uart.c
    ${PROJECT_SOURCE_DIR}/App/Src/uart.c
)
```

> `UNIT_TEST` define передаётся в тестируемый код — используй его для
> compile-time seam при работе со `static` функциями.

---

## 12. Запуск тестов

```bash
# 1. Конфигурация
cmake --preset host-debug

# 2. Сборка
cmake --build --preset host-debug-build

# 3. Запуск всех тестов
ctest --preset host-debug-test

# Запуск конкретного теста с полным выводом
ctest --preset host-debug-test -R test_led -V

# Запуск напрямую — полный вывод Unity
./build-host-debug/Tests/host/test_led

# Одной командой
cmake --preset host-debug && \
cmake --build --preset host-debug-build && \
ctest --preset host-debug-test
```

### Пример вывода при успехе

```
test_led.c:28:test_led_on_sets_gpio_high:PASS
test_led.c:35:test_led_off_sets_gpio_low:PASS

-----------------------
2 Tests 0 Failures 0 Ignored
OK
```

### Пример вывода при провале

```
test_led.c:29:test_led_on_sets_gpio_high:FAIL:
  Expected 1 Was 0

-----------------------
2 Tests 1 Failures 0 Ignored
FAIL
```
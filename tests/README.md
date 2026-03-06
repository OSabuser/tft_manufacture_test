# Юнит тестирование CTest, Unity, CMakePresets

- [Юнит тестирование CTest, Unity, CMakePresets](#юнит-тестирование-ctest-unity-cmakepresets)
  - [1. Общая архитектура](#1-общая-архитектура)
    - [1.1 Структура проекта](#11-структура-проекта)
    - [1.2 CMake конфигурация](#12-cmake-конфигурация)
    - [1.3 CMake Presets](#13-cmake-presets)
  - [2. Использование](#2-использование)
    - [2.1 CLI](#21-cli)
  - [3. Ключевые принципы](#3-ключевые-принципы)
  - [4. Итоговый Workflow](#4-итоговый-workflow)

## 1. Общая архитектура

| Компонент                | Назначение                                    | Где используется |
| ------------------------ | --------------------------------------------- | ---------------- |
| **Unity**                | Фреймворк unit-тестирования (ThrowTheSwitch)  | Host + Target    |
| **SEGGER RTT**           | Вывод логов через отладчик                    | Только Target    |
| **ThirdParty INTERFACE** | Централизованная точка для всех зависимостей  | Везде            |
| **CMake Presets**        | Конфигурационные профили для разных окружений | CLI + VSCode     |
| **CTest**                | Запуск и отчёты по тестам                     | Host тесты       |

### 1.1 Структура проекта

```bash
ProjectRoot/
├── CMakeLists.txt                    # Корневой CMake
├── CMakePresets.json                 # Presets для host/target
├── cmake/
│   └── toolchain-stm32f4.cmake       # ARM GCC toolchain
│
├── App/                              # Код приложения
│   ├── Src/
│   │   └── app.c                     # Тестируемый код
│   └── Inc/
│       └── app.h
│
├── Tests/                            # Все тесты
│   ├── CMakeLists.txt                # enable_testing() + опции
│   ├── host/                         # Host тесты (Mac)
│   │   ├── CMakeLists.txt
│   │   └── test_host_simple.c
│   └── target/                       # Target тесты (STM32)
│       ├── CMakeLists.txt
│       ├── test_main.c               # Unity runner для МК
│       └── test_gpio.c
│
├── ThirdParty/                       # Внешние зависимости
│   ├── CMakeLists.txt                # INTERFACE библиотека
│   ├── Unity/                        # git submodules
│   │   └── src/
│   │       ├── unity.c
│   │       └── unity.h
│   └── SEGGER_RTT/
│       ├── RTT/
│       └── wrapper/
│           └── CMakeLists.txt        # PUBLIC include пути
│
├── build-host/                       # Сборка для Mac (генерируется)
└── build-target/                     # Сборка для STM32 (генерируется)
```

### 1.2 CMake конфигурация

1. Корневой **CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.22)
project(MySTM32Project C ASM)

# Добавляем все компоненты
add_subdirectory(ThirdParty)  # Зависимости
add_subdirectory(App)         # Код приложения
add_subdirectory(Tests)       # Тесты
```

1. ThirdParty/CMakeLists.txt (**INTERFACE** библиотека)

 • INTERFACE  библиотека НЕ генерирует  .a/.so  файлы
 • Служит контейнером для передачи зависимостей
 • Наследует PUBLIC свойства от связанных библиотек
 • Передаёт всё потребителям одной строкой  target_link_libraries(MyApp ThirdParty)

Как работает **INTERFACE**:

```txt
1. ThirdParty INTERFACE создана (без файлов)
2. target_link_libraries(ThirdParty INTERFACE SeggerRTT)
   → ThirdParty наследует PUBLIC свойства SeggerRTT:
      - include пути: RTT/
      - defines: RTT_MODE=1
      - библиотеку: libSeggerRTT.a
3. target_link_libraries(MyApp ThirdParty)
   → MyApp автоматически получает ВСЁ от ThirdParty
```

1. **Tests/CMakeLists.txt** (входная точка для тестов)

```cmake
# ⚠️ ОБЯЗАТЕЛЬНО! Регистрирует тесты в CTest
enable_testing()

# Опции для выбора типа тестов
option(BUILD_TESTS_HOST "Build host (Mac) unit tests" ON)
option(BUILD_TESTS_TARGET "Build STM32 target tests" OFF)

# Host тесты (Mac)
if(BUILD_TESTS_HOST)
    message(STATUS "🔧 Building host tests")
    add_subdirectory(host)
endif()

# Target тесты (STM32)
if(BUILD_TESTS_TARGET)
    message(STATUS "🔧 Building target tests")
    add_subdirectory(target)
endif()
```

1. **Tests/host/CMakeLists.txt** (host тесты)

```cmake
# Тестовый исполняемый файл
add_executable(test_host_simple 
    test_host_simple.c                    # Код тестов
    ${PROJECT_SOURCE_DIR}/App/Src/app.c   # Тестируемый код
)

# Include пути
target_include_directories(test_host_simple PRIVATE 
    ${PROJECT_SOURCE_DIR}/App/Inc                   # Заголовки приложения
    ${PROJECT_SOURCE_DIR}/ThirdParty/Unity/src      # unity.h
)

# Линковка с ThirdParty → автоматически получаем Unity
target_link_libraries(test_host_simple PRIVATE ThirdParty)

# ⚠️ КРИТИЧНО! Регистрация в CTest
add_test(NAME test_host_simple 
         COMMAND test_host_simple
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR})  # build-host/
```

1. **Tests/target/CMakeLists.txt** (target тесты)

```cmake
# Тестовый ELF для STM32
add_executable(test_gpio.elf
    test_main.c                               # Unity runner
    test_gpio.c                               # Тесты GPIO
    ${PROJECT_SOURCE_DIR}/Src/hw/gpio_driver.c
)

target_include_directories(test_gpio.elf PRIVATE 
    ${PROJECT_SOURCE_DIR}/Src/hw
)

# ThirdParty даёт Unity + RTT
target_link_libraries(test_gpio.elf PRIVATE ThirdParty)

# Линкер скрипт для STM32
target_link_options(test_gpio.elf PRIVATE
    -T${PROJECT_SOURCE_DIR}/cmake/STM32F407VETx_flash.ld
    -Wl,--gc-sections
    -Wl,--print-memory-usage
)

# Отчёт размера
add_custom_command(TARGET test_gpio.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:test_gpio.elf>
)
```

### 1.3 CMake Presets

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "host-debug",
      "displayName": "🖥️ Host Debug Tests",
      "description": "Unit tests for Mac (AppleClang)",
      "binaryDir": "${sourceDir}/build-host",
      "generator": "Ninja",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "BUILD_TESTS_HOST": "ON",
        "BUILD_TESTS_TARGET": "OFF",
        "UNITY_ENABLED": "ON",
        "SEGGER_RTT_ENABLED": "OFF"
      }
    },
    {
      "name": "target-debug",
      "displayName": "🎯 STM32F407 Target Tests",
      "description": "Integration tests for STM32",
      "binaryDir": "${sourceDir}/build-target",
      "generator": "Ninja",
      "toolchainFile": "${sourceDir}/cmake/toolchain-stm32f4.cmake",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "BUILD_TESTS_HOST": "OFF",
        "BUILD_TESTS_TARGET": "ON",
        "UNITY_ENABLED": "ON",
        "SEGGER_RTT_ENABLED": "ON"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "host-debug-build",
      "configurePreset": "host-debug",
      "displayName": "Build Host Tests",
      "targets": ["test_host_simple"]
    },
    {
      "name": "target-debug-flash",
      "configurePreset": "target-debug",
      "displayName": "Flash Target Tests",
      "targets": ["test_gpio.elf"]
    }
  ],
  "testPresets": [
    {
      "name": "host-debug-test",
      "configurePreset": "host-debug",
      "displayName": "🧪 Run Host Tests",
      "output": {
        "outputOnFailure": true,
        "verbosity": "detailed"
      }
    }
  ]
}
```

Что делают **presets**:

| Preset       | Компилятор | Опции                 | Результат                   |
| ------------ | ---------- | --------------------- | --------------------------- |
| host-debug   | AppleClang | BUILD_TESTS_HOST=ON   | build-host/test_host_simple |
| target-debug | ARM GCC    | BUILD_TESTS_TARGET=ON | build-target/test_gpio.elf  |

## 2. Использование

### 2.1 CLI

**Host** тесты

```bash
# 1. Конфигурация
cmake --preset host-debug

# 2. Сборка
cmake --build --preset host-debug-build

# 3. Запуск тестов
ctest --preset host-debug-test

# Или всё одной командой:
cmake --preset host-debug && \
cmake --build --preset host-debug-build && \
ctest --preset host-debug-test
```

**Target** тесты

```bash
# 1. Конфигурация
cmake --preset target-debug

# 2. Сборка
cmake --build --preset target-debug-flash

# 3. Прошивка и запуск
probe-rs run --chip STM32F407VETx build-target/test_gpio.elf

# Смотрим вывод RTT в терминале!
```

## 3. Ключевые принципы

1. **INTERFACE** библиотека **ThirdParty**
 • Контейнер зависимостей без генерации файлов
 • Наследует **PUBLIC** свойства от связанных библиотек
 • Передаёт всё одной строкой  `target_link_libraries(App ThirdParty)`
2. **CMake Presets**
 • Один файл для всех конфигураций (host/target)
 • Опции управляют включением тестов ( BUILD_TESTS_HOST/TARGET )
 • Build presets с  targets  предотвращают сборку лишнего
3. **CTest** интеграция
 •  `enable_testing()`  в  `Tests/CMakeLists.txt`
 •  `add_test()`  для каждого теста
 •  `WORKING_DIRECTORY ${CMAKE_BINARY_DIR}`  критично!
4. Фиксы **Unity**
 • Явное  `target_include_directories`  для  `Unity/src`
 • `Unity CMakeLists.txt` не экспортирует **PUBLIC** пути

## 4. Итоговый Workflow

| Шаг   | Действие                                                                              |
| :---- | ------------------------------------------------------------------------------------- |
| **1** | Написали код в `App/Src/app.c`                                                        |
| **2** | Написали `host` тест в `Tests/host/test_host_simple.c`                                |
| **3** | `cmake --preset host-debug` → AppleClang, build-host/, Unity enabled                  |
| **4** | `cmake --build --preset host-debug-build` → Только test_host_simple собирается        |
| **5** | `ctest --preset host-debug-test`    → Тесты выполняются, отчёт CTest                  |
| **6** | Всё работает! Добавили target тест в `Tests/target/`                                  |
| **7** | `cmake --preset target-debug` → ARM GCC, build-target/, Unity + RTT enabled           |
| **8** | `cmake --build --preset target-debug-flash`   → `test_gpio.elf` создан                |
| **9** | `probe-rs run --chip STM32F407VETx test_gpio.elf` → Прошивка, запуск, вывод через RTT |

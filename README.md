# tft_manufacture_test

Монорепозиторий для **MIMXRT1052CVJ5B**. Содержит три независимых firmware-проекта с общей инфраструктурой сборки, тестирования и инструментарием.

> Архитектура рабочего окружения разработчика — [docs/DEV_ARCH.md](docs/DEV_ARCH.md)

---

## Структура репозитория

```bash
/
├── .devcontainer/              # VSCode Devcontainer — единое окружение для всех разработчиков
│   ├── Dockerfile
│   └── devcontainer.json
├── .vscode/
│   ├── launch.json
│   └── tasks.json              # UI для just build::* (внутри devcontainer)
├── bsp/                        # Board Support Package
│   ├── CMakeLists.txt
│   ├── common/                 # Общие типы (bsp_status_t и др.)
│   ├── generated/              # Сгенерировано NXP Config Tools (Pins + Clocks Tool)
│   │   ├── TFT_Board.mex       # Источник истины конфигурации пинов и тактирования
│   │   ├── pin_mux.c/h         # Сгенерировано из .mex (Pins Tool)
│   │   ├── clock_config.c/h    # Сгенерировано из .mex (Clocks Tool)
│   │   ├── board.c/h           # Ручная инициализация специфики платы
│   │   ├── syscalls.c          # Заглушки системных вызовов newlib
│   │   └── startup/            # Стартап-файл для ARM
│   ├── led/                    # bsp_led — два UserLed (GPIO3_IO03, GPIO3_IO04)
│   ├── tick/                   # bsp_tick — SysTick / FreeRTOS-совместимый таймер
│   ├── uart_host/              # bsp_uart_host — LPUART1 (MCU-Link VCOM, J2)
│   │   └── mocks/              # Мок-реализация для host-тестов
│   └── usb_cdc/                # bsp_usb_cdc — USB CDC ACM
├── cmake/                      # Общие CMake модули
│   ├── linker/                 # Линкер-скрипты (ram, flexspi_nor, sdram и др.)
│   ├── toolchain_arm.cmake     # ARM cross-compilation toolchain
│   └── toolchain_host.cmake    # Host GCC для unit-тестов
├── sdk/                        # NXP MCUXpresso SDK — vendored 
├── lib/                        # Внешние библиотеки — vendored
│   ├── Unity/                  # Фреймворк для unit-тестов
│   ├── fff/                    # Fake Function Framework для моков
│   └── SEGGER/                 # SEGGER RTT — вывод логов через отладчик
├── firmware/
│   ├── test/                   # [Проект 1] Тестовая прошивка — входной контроль платы
│   ├── bootloader/             # [Проект 2] Загрузчик с поддержкой A/B обновления
│   └── tft_app/                # [Проект 3] Основная боевая прошивка (FreeRTOS)
├── tests/
│   ├── CMakeLists.txt
│   ├── host/                   # Unit-тесты на хостовом компиляторе (Unity + fff)
│   │   ├── mocks/              # Stub-хедеры NXP SDK для компиляции на хосте
│   │   ├── led/                # Тесты bsp_led
│   │   ├── ring_buffer/        # Тесты ring_buffer
│   │   ├── timeout/            # Тесты таймаут-паттерна
│   │   └── uart_host/          # Тесты bsp_uart_host (через мок)
│   ├── target/                 # HIL target-прошивки (загружаются в RAM через pyOCD)
│   │   └── host_uart/          # CLI-прошивка для тестирования bsp_uart_host
│   ├── HOST_CREATE_TEST.md     # Гайд: добавление host-теста
│   └── HIL_CREATE_TEST.md      # Гайд: добавление HIL-теста
├── tools/
│   ├── host/                   # Инструменты прошивки (spsdk)
│   │   ├── flash_usb.py        # Прошивка через USB ROM (sdphost + blhost)
│   │   ├── hab/                # HAB yaml-конфиги для nxpimage
│   │   ├── dcd/                # ivt_flashloader.bin, dcd.bin
│   │   ├── pyproject.toml
│   │   └── uv.lock
│   └── hil/                    # HIL-тесты (pytest + pyOCD + pyserial)
│       ├── pyproject.toml      
│       ├── conftest.py         # Фикстуры: загрузка ELF + UART
│       ├── pyocd_utils.py      # FLEXRAM init, ELF loader, run_from_vectors
│       ├── env_config.py       # Конфигурация из os.environ / .env
│       ├── load_and_run.py     # CLI-утилита для ручной загрузки ELF в RAM микроконтроллера
│       └── test_uart.py        # Тесты bsp_uart_host (PING/ECHO/BUF_SIZE)
├── utils/
│   └── ring_buffer/            # Платформонезависимый кольцевой буфер
├── just/
│   ├── build.just              # devcontainer: сборка, тесты, HAB, HIL-прошивки
│   ├── host.just               # хост: прошивка, bootstrap, HIL-запуск
│   └── ci.just                 # CI/CD пайплайны
├── docs/
│   ├── DEV_ARCH.md             
│   ├── CMAKE_HINTS.md          
│   ├── HOW_TO_FLASH.md         
│   ├── BOOT_FLAGS.md           # Флаги загрузчика
│   └── schematic.pdf           # Схема платы
├── pyocd.yaml                  # Конфигурация pyOCD (target: cortex_m, RAM-режим)
├── .env                        # Конфигурация проекта (VID:PID, HIL-порты и др.)
├── .env.example                # Шаблон .env для новых разработчиков
├── bootstrap.sh                # Первичная настройка окружения (уровень 0)
├── CMakeLists.txt              # Корневой CMake
├── CMakePresets.json           # Пресеты сборки (Debug/Release/Host/Target)
└── justfile                    # Точка входа для команд (модули: build, host, ci)
```

---

## Три firmware-проекта

### 1. Тестовая прошивка (`firmware/test/`)

Bare-metal прошивка для **входного контроля** платы. Проверяет базовую работоспособность всех интерфейсов: CAN, UART, SDRAM, QSPI Flash, uSD (SDIO), RGB-интерфейс, гальванически развязанные входы, светодиоды, кнопки, IR-приёмник, MQS.

Загружается через USB ROM (SDP) — подробнее в [docs/HOW_TO_FLASH.md](docs/HOW_TO_FLASH.md).

### 2. Загрузчик (`firmware/bootloader/`)

Отвечает за обновление боевой прошивки в полевых условиях. Поддерживает схему **A/B** с обновлением через uSD. Обновление самого загрузчика — только через USB ROM + blhost, не через себя.

### 3. Боевая прошивка (`firmware/tft_app/`)

Основная прошивка на базе **FreeRTOS**. Включает FatFS, бизнес-логику, модули. Обновляется через загрузчик по схеме A/B.

---

## Тестирование

Стратегия тестирования трёхуровневая:

| Уровень | Расположение | Инструменты | Запуск |
|---------|-------------|-------------|--------|
| **Host-тесты** (unit) | `tests/host/` | Unity + fff | `just build::test-host` в devcontainer |
| **HIL target-тесты** (аппаратные) | `tests/target/` + `tools/hil/` | pyOCD + pyserial + pytest | `just host::hil-run` на хосте |

### Host-тесты

Компилируются и выполняются в devcontainer на хостовом компиляторе. Железо не нужно. BSP-модули тестируются через fff-фейки и stub-хедеры из `tests/host/mocks/`.

Гайд по добавлению нового теста — [tests/HOST_CREATE_TEST.md](tests/HOST_CREATE_TEST.md).

### HIL target-тесты

Каждый HIL-тест — это пара: **C-прошивка** (`tests/target/<n>/`) с текстовым CLI через UART и **pytest-тесты** (`tools/hil/test_<n>.py`). pyOCD загружает `.elf` в RAM через MCU-Link (CMSIS-DAP), pytest общается с прошивкой через MCU-Link VCOM.

```bash
pytest → uart_cmd("PING\r\n") → MCU-Link VCOM → RT1052 → "PONG\r\n" → pytest
```

Гайд по добавлению нового теста — [tests/HIL_CREATE_TEST.md](tests/HIL_CREATE_TEST.md).

---

## Управление зависимостями

| Зависимость | Подход | Причина |
|-------------|--------|---------|
| NXP MCUXpresso SDK | vendored | Стабильная версия, обновлений не планируется |
| FreeRTOS, FatFS, LittleFS и др. | vendored (через SDK) | Стабильные версии |
| Unity + fff | vendored | Маленькие, стабильные |
| SEGGER RTT | vendored | Стабильный |
| pyOCD, pyserial, pytest | `tools/hil/uv.lock` | Фиксированные версии |
| spsdk (nxpimage, blhost) | `tools/host/uv.lock` | Фиксированные версии |

**Принцип:** всё что не меняется — vendored. Полностью автономная сборка после `git clone` без доступа к интернету (кроме Python-зависимостей).

---

## Devcontainer — состав окружения

| Инструмент | Назначение |
|------------|------------|
| `arm-none-eabi-gcc` | Сборка firmware и HIL target-прошивок для ARM |
| `arm-none-eabi-gdb` | Отладка через GDB server |
| `gcc` / `clang` (host) | Сборка и запуск host-тестов |
| `CMake + Ninja` | Система сборки |
| `CTest` | Запуск host-тестов |
| `clangd` | Language server для VSCode |
| `clang-format` | Форматирование кода |
| `clang-tidy` | Статический анализ |
| `Python 3 + nxp-spsdk` | HAB-образы (nxpimage) |
| `just` | Запуск рецептов через модули `build::` |

---

## Быстрый старт

```bash
git clone <repo-url>
cd tft_manufacture_test

# Инициализация хоста (один раз)
./bootstrap.sh

# Открыть в VSCode → Reopen in Container
# Затем внутри devcontainer:
just build::test-host                    # host unit-тесты
just build::build-firmware-test-debug    # сборка firmware
just build::hab-firmware-test-debug      # подготовка HAB-образа
just build::build-hil                    # сборка HIL target-прошивок

# На хосте (вне контейнера):
just flash                               # прошить firmware_test debug во Flash
just host::hil-run                       # загрузить HIL ELF + запустить pytest
```

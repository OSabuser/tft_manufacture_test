# tft_manufacture_test

Монорепозиторий для **MIMXRT1052CVJ5B**. Содержит три независимых firmware-проекта с общей инфраструктурой сборки, тестирования и инструментарием.

> Архитектура рабочего окружения разработчика — [docs/DEV_ARCH.md](docs/DEV_ARCH.md)

---

## Структура репозитория

```bash
/
├── .devcontainer/              # VSCode Devcontainer — единое окружение для всех разработчиков
├── bsp/                        # Board Support Package
│   └── generated/              # Сгенерировано NXP Config Tools (Pins + Clocks Tool)
│       ├── TFT_Board.mex       # Источник истины конфигурации пинов и тактирования
│       ├── pin_mux.c/h         # Сгенерировано из .mex (Pins Tool)
│       ├── clock_config.c/h    # Сгенерировано из .mex (Clocks Tool)
│       ├── board.c/h           # Ручная инициализация специфики платы
│       └── BOOT_FLAGS.md       # Описание флагов загрузчика
├── cmake/                      # Общие CMake модули и toolchain files
│   ├── linker/                 # Линкер-скрипты под разные схемы размещения
│   ├── toolchain_arm.cmake     # ARM cross-compilation toolchain
│   └── toolchain_host.cmake    # Host GCC для unit-тестов
├── sdk/                        # NXP MCUXpresso SDK — vendored
│   ├── CMakeLists.txt
│   ├── CMSIS/
│   ├── devices/MIMXRT1052/     # Драйверы, startup, утилиты
│   ├── components/             # fsl_button, fsl_led, serial_manager и др.
│   ├── middleware/             # FatFS, FreeRTOS, LittleFS, USB, mcuboot и др.
│   └── rtos/freertos/          # FreeRTOS (vendored через SDK)
├── lib/                        # Внешние библиотеки
│   ├── Unity/                  # Фреймворк для unit-тестов (vendored)
│   ├── fff/                    # Fake Function Framework для моков (vendored)
│   └── SEGGER/                 # SEGGER RTT — вывод логов через отладчик
├── firmware/
│   ├── test/                   # [Проект 1] Тестовая прошивка — входной контроль платы
│   ├── bootloader/             # [Проект 2] Загрузчик с поддержкой A/B обновления
│   └── tft_app/                # [Проект 3] Основная боевая прошивка (FreeRTOS)
├── tests/                      # Тесты (host + target)
│   ├── host/                   # Unit/интеграционные тесты, запускаемые на хосте
│   ├── target/                 # Тесты периферии, запускаемые на таргете
│   └── HostTestingGuide.md
├── tools/
│   └── host/                   # Инструменты для работы с таргетом
│       ├── flash_usb.py        # Прошивка через USB ROM (nxp-spsdk / blhost)
│       ├── hab/                # Утилиты и гайд по HAB (Secure Boot)
│       └── dcd/                # Device Configuration Data
├── just/                       # Just-модули (автоматизация)
│   ├── build.just              # Сборка, тесты, HAB-образы (devcontainer)
│   ├── host.just               # Прошивка, bootstrap, HIL (хост)
│   └── ci.just                 # CI/CD пайплайны
├── scripts/
│   └── bootstrap.sh            # Первичная настройка окружения (уровень 0)
├── docs/                       # Документация проекта
│   ├── DEV_ARCH.md             # Архитектура окружения разработки
│   ├── CMAKE_HINTS.md          # Шпаргалка по CMake в проекте
│   ├── schematic.pdf           # Схема платы
│   ├── mcu_rm.pdf              # Reference Manual IMXRT1052
│   └── manufacturing_user's_guide.pdf
├── CMakeLists.txt              # Корневой CMake
├── CMakePresets.json           # Пресеты сборки (Release/Debug/Host)
├── Justfile                    # Точка входа для команд (модули: build, host, ci)
└── README.md
```

---

## Три firmware-проекта

### 1. Тестовая прошивка (`firmware/test/`)

Bare-metal прошивка для **входного контроля** платы. Проверяет базовую работоспособность всех интерфейсов: CAN, UART, SDRAM, QSPI Flash, uSD (SDIO), RGB-интерфейс, гальванически развязанные входы, светодиоды, кнопки, IR-приёмник, MQS.

Загружается через USB ROM (Serial Download Mode) — подробнее в [HOW_TO_FLASH.md](HOW_TO_FLASH.md).

> **Рекомендуется начать разработку с этого проекта** — он наиболее прост и позволяет полностью отладить окружение сборки и прошивки.

### 2. Загрузчик (`firmware/bootloader/`)

Отвечает за обновление боевой прошивки в полевых условиях. Поддерживает схему **A/B** с обновлением через uSD. Обновление самого загрузчика — только через внешний инструмент (USB ROM + blhost), не через себя. На производстве загружается единым blob-ом вместе с первой версией боевой прошивки.

### 3. Боевая прошивка (`firmware/tft_app/`)

Основная прошивка на базе **FreeRTOS**. Включает FatFS, бизнес-логику, модули. Обновляется через загрузчик по схеме A/B.

---

## Тестирование

Стратегия тестирования двухуровневая:

| Уровень                                | Расположение    | Инструменты     | Запуск                                 |
| -------------------------------------- | --------------- | --------------- | -------------------------------------- |
| **Host-тесты** (unit + интеграционные) | `tests/host/`   | Unity + fff     | `just build::test-host` в devcontainer |
| **Target-тесты** (аппаратные)          | `tests/target/` | Unity на железе | Удалённый ПК-сервер через SSH          |

Подробнее — [tests/HostTestingGuide.md](tests/HostTestingGuide.md) и [tests/README.md](tests/README.md).

---

## Управление зависимостями

| Зависимость                     | Подход               | Причина                                      |
| ------------------------------- | -------------------- | -------------------------------------------- |
| NXP MCUXpresso SDK              | vendored             | Стабильная версия, обновлений не планируется |
| FreeRTOS, FatFS, LittleFS и др. | vendored (через SDK) | Стабильные версии                            |
| Unity + fff                     | vendored             | Маленькие, стабильные                        |
| SEGGER RTT                      | vendored             | Стабильный                                   |

**Принцип:** всё что не меняется — vendored (закоммичено в репозиторий). Это обеспечивает полностью автономную сборку после `git clone` без доступа к интернету.

---

## Devcontainer — состав окружения

| Инструмент             | Назначение                             |
| ---------------------- | -------------------------------------- |
| `arm-none-eabi-gcc`    | Сборка firmware для таргета            |
| `arm-none-eabi-gdb`    | Отладка через GDB server (удалённая)   |
| `gcc` (host)           | Сборка и запуск host-тестов            |
| `CMake + Ninja`        | Система сборки                         |
| `CTest`                | Запуск тестов (Unity + fff)            |
| `clangd`               | Language server для VSCode             |
| `clang-format`         | Форматирование кода                    |
| `clang-tidy`           | Статический анализ                     |
| `Python 3 + nxp-spsdk` | HAB-образы (nxpimage)                  |
| `just`                 | Запуск рецептов через модули `build::` |

---

## Конфигурация платы (NXP Config Tools)

Файл `bsp/generated/TFT_Board.mex` — **источник истины** для конфигурации пинов и тактирования. Открывается в NXP Config Tools (Pins Tool + Clocks Tool) для регенерации `pin_mux.c/h` и `clock_config.c/h`. Используется при старте проекта или при изменении аппаратной схемы. Коммитится вместе со сгенерированным кодом.

---

## Быстрый старт

```bash
git clone <repo-url>
cd tft_manufacture_test

# Инициализация хоста (один раз)
sudo chmod +x bootstrap.sh
./bootstrap.sh

# Открыть в VSCode → Reopen in Container
# Затем внутри devcontainer:

just build::test-host              # сборка и запуск host-тестов
just build::build-firmware-test-debug  # сборка firmware для таргета
just build::hab-firmware-test-debug    # подготовка HAB-образа

# На хосте (вне контейнера):
just flash                         # прошивка через USB ROM
```

> Подробнее о прошивке — [HOW_TO_FLASH.md](HOW_TO_FLASH.md)
> Подробнее об окружении разработки — [docs/DEV_ARCH.md](docs/DEV_ARCH.md)

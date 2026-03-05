# tft_manufacture_test

## Структура проекта

Монорепозиторий для MIMXRT1052CVJ5B. Три независимых firmware-проекта с общей инфраструктурой сборки, тестирования и инструментарием.

---

## Структура репозитория

```bash
/
├── .devcontainer/              # VSCode Devcontainer — единое окружение для всех разработчиков
├── cmake/                      # Общие CMake модули и toolchain files
│   ├── toolchain_arm.cmake     # ARM cross-compilation toolchain
│   └── toolchain_host.cmake    # Host GCC для unit-тестов
├── sdk/                        # NXP MCUXpresso SDK — vendored, только нужные компоненты
│   ├── CMakeLists.txt          # CMake-таргеты для каждого драйвера
│   ├── CMSIS/
│   ├── devices/
│   │   └── MIMXRT1052/
│   │       ├── drivers/        # fsl_flexcan, fsl_lpuart, fsl_usdhc и др.
│   │       ├── startup/
│   │       └── utilities/
│   └── components/
├── bsp/                        # Board Support Package
│   └── board/
│       ├── board.mex           # Исходник конфигурации для NXP Config Tools
│       ├── pin_mux.c/h         # Сгенерировано из board.mex (Pins Tool)
│       ├── clock_config.c/h    # Сгенерировано из board.mex (Clocks Tool)
│       └── board.c/h           # Ручная инициализация специфики платы
├── lib/                        # Библиотеки и зависимости
│   ├── CMakeLists.txt          # Агрегатор — подключает нужные модули через опции
│   ├── freertos/               # vendored
│   ├── fatfs/                  # vendored
│   ├── mbedtls/                # vendored (если используется)
│   ├── unity/                  # vendored (test framework)
│   ├── fff/                    # vendored (fake functions для тестов)
│   └── hal/                    # submodule — аппаратно-независимые библиотеки
│                               # (второй разработчик, активно развивается)
├── firmware/
│   ├── test/                   # [Проект 1] Тестовая прошивка — входной контроль
│   ├── bootloader/             # [Проект 2] Загрузчик с поддержкой A/B обновления
│   └── app/                    # [Проект 3] Основная боевая прошивка (FreeRTOS)
├── tests/                      # Host-тесты (unit + integration)
│   ├── unit/
│   └── integration/
└── tools/                      # Скрипты для прошивки, провизии, HIL-тестов
    ├── flash_usb.py            # Прошивка через USB ROM (blhost / nxp-spsdk)
    ├── flash_remote.sh         # Прошивка на удалённый сервер через SSH
    └── provision.py            # Производственная провизия (SPT / nxpimage)
```

---

## Три firmware-проекта

### 1. Тестовая прошивка (`firmware/test/`)

Bare-metal прошивка для входного контроля на производстве. Проверяет базовую работоспособность всех интерфейсов и периферии: CAN, UART, SDRAM, QSPI Flash, uSD, RGB-интерфейс, гальванически развязанные входы, светодиоды, кнопки. Загружается через USB ROM (Serial Download Mode). **Рекомендуется начать разработку с этого проекта** — он наиболее прост и позволяет полностью отладить окружение сборки.

### 2. Загрузчик (`firmware/bootloader/`)

Отвечает за обновление боевой прошивки в полевых условиях. Поддерживает схему A/B с шифрованием (обновление через uSD). Обновление самого загрузчика — только через внешний инструмент (USB ROM + blhost), не через себя. Загружается на производстве вместе с первой версией боевой прошивки единым blob-ом.

### 3. Боевая прошивка (`firmware/app/`)

Основная прошивка на базе FreeRTOS. Включает FatFS, бизнес-логику, модули. Обновляется через загрузчик по схеме A/B.

---

## Управление зависимостями

| Зависимость | Подход | Причина |
|---|---|---|
| NXP SDK | vendored | Стабильная версия, обновлений не планируется |
| FreeRTOS | vendored | Стабильная версия |
| FatFS, mbedTLS и др. | vendored | Стабильные версии |
| Unity + fff | vendored | Маленькие, стабильные |
| lib/hal (второй разработчик) | **submodule** | Активно развивается параллельно |

**Принцип:** всё что не меняется — vendored (скачано и закоммичено). Submodule только для активно развивающихся зависимостей. Это обеспечивает полностью автономную сборку после `git clone` без доступа к интернету.


## Devcontainer — состав окружения

| Инструмент | Назначение |
|---|---|
| `arm-none-eabi-gcc` | Сборка firmware для таргета |
| `arm-none-eabi-gdb` | Отладка через GDB server |
| `host-gcc` | Сборка и запуск host-тестов |
| `CMake + Ninja` | Система сборки |
| `CTest` | Запуск тестов (Unity + fff) |
| `clangd` | Language server для VSCode |
| `clang-format` | Форматирование кода |
| `clang-tidy` | Статический анализ |
| `cppcheck` | Дополнительный статический анализ |
| `lcov / gcovr` | Покрытие host-тестов |
| `Python 3 + nxp-spsdk` | Прошивка (blhost, nxpimage), производственная провизия |
| `srec_cat` | Манипуляции с бинарными образами |

---

## Конфигурация платы (NXP Config Tools)

Файл `bsp/board/board.mex` — источник истины для конфигурации пинов и тактирования. Открывается в NXP Config Tools (Pins Tool + Clocks Tool) для генерации `pin_mux.c/h` и `clock_config.c/h`. Используется **один раз** при старте проекта или при изменении аппаратной схемы. Коммитится в репозиторий вместе со сгенерированным кодом.

---

## Доставка прошивки

| Сценарий | Способ |
|---|---|
| Разработка (с ПК разработчика) | `rsync` / `scp` → SSH на ПК-сервер → OpenOCD/blhost |
| Производство | `nxp-spsdk` (blhost + nxpimage) через USB ROM |
| Готовое ПО на сервер | GitLab CI/CD → GitLab Package Registry → сервер подтягивает |
| Обновление загрузчика | USB ROM + blhost (только так, не через сам загрузчик) |
| Обновление боевой прошивки в поле | Через загрузчик, схема A/B, uSD |
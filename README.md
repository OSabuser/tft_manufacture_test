# tft_manufacture_test

Монорепозиторий для **MIMXRT1052CVJ5B**. Содержит три независимых firmware-проекта
с общей инфраструктурой сборки, тестирования и инструментарием.

> Архитектура рабочего окружения — [docs/DEV_ARCH.md](docs/DEV_ARCH.md)

---

## Firmware-проекты

| Проект                              | Путь                   | Описание                                                                                                                                                               |
| ----------------------------------- | ---------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Тестовая прошивка                   | `firmware/test/`       | Входной контроль платы: CAN, UART, SDRAM, QSPI, SDIO, RGB, оптовходы, LED, кнопки, MQS [README](firmware/test/README.md)                                               |
| Загрузчик                           | `firmware/bootloader/` | A/Б обновление через uSD (MCUboot, Direct-XIP), recovery при зависании образа. Обновляется только через USB ROM + blhost / SWD [README](firmware/bootloader/README.md) |
| Production прошивка (запланирована) | `firmware/tft_app/`    | Приложение с реализацией логики лифтового индикатора. Обновляется загрузчиком                                                                                          |

---

## Инструменты (`tools/`)

| Инструмент         | Путь                 | Назначение                                                                                     |
| ------------------ | -------------------- | ---------------------------------------------------------------------------------------------- |
| Сервисный TUI      | `tools/service_tui/` | Диагностика и прошивка готовых плат сервисным инженером  [README](tools/service_tui/README.md) |
| Прошивка (dev-CLI) | `tools/host/`        | USB SDP / SWD прошивка при разработке  [README](tools/host/README.md)                          |
| HIL-тесты          | `tools/hil/`         | pytest-окружение аппаратных тестов [README](tools/hil/README.md)                               |

---

## BSP

Описание модулей, правила написания компонентов и CMake-шаблоны — [bsp/README.md](bsp/README.md).

---

## Тестирование

| Уровень          | Инструменты                            | Запуск                                 |
| ---------------- | -------------------------------------- | -------------------------------------- |
| Host unit-тесты  | Unity + fff, clang                     | `just build::test-host` (devcontainer) |
| HIL target-тесты | pyOCD + pyserial + pytest + M5StampPLC | `just host::hil-run` (хост)            |

- Как добавить host-тест — [docs/testing/host/HOST_CREATE_TEST.md](docs/testing/host/HOST_CREATE_TEST.md)
- Как добавить HIL-тест — [docs/testing/hil/HIL_CREATE_TEST.md](docs/testing/hil/HIL_CREATE_TEST.md)
- Как запустить HIL-тесты — [docs/testing/hil/HIL_HOW_TO.md](docs/testing/hil/HIL_HOW_TO.md)
- HIL стенд и подключение — [docs/testing/hil/HIL_BENCH.md](docs/testing/hil/HIL_BENCH.md)

---

## Сборка и прошивка

```bash
# devcontainer
just build::test-host                   # host unit-тесты
just build::build-firmware-test-debug   # ELF
just build::hab-firmware-test-debug     # HAB-образ для прошивки
just build::build-hil                   # HIL target-прошивки

# хост
just host::flash-test-debug             # прошить через USB SDP
just host::flash-swd-test-debug         # прошить через SWD (power cycle после)
just host::hil-run                      # HIL-тесты
just host::debug-server                 # GDB-сервер для отладки
```

Прошивка подробно — [docs/HOW_TO_FLASH.md](docs/HOW_TO_FLASH.md)
Отладка подробно — [docs/HOW_TO_DEBUG.md](docs/HOW_TO_DEBUG.md)
Выпуск релизов — [docs/RELEASE_PROCESS.md](docs/RELEASE_PROCESS.md) (порядок действий),
[docs/CI_WORKFLOW.md](docs/CI_WORKFLOW.md) (устройство workflow)

---

## Быстрый старт

```bash
git clone <repo-url>
cd tft_manufacture_test

# 1. Инициализация хоста (один раз)
./bootstrap.sh

# 2. Заполнить .env (порты MCU-Link и M5StampPLC)
cp .env.example .env

# 3. Задеплоить агент на M5StampPLC (один раз)
just host::m5-deploy

# 4. Открыть в VSCode → Reopen in Container
# Затем внутри devcontainer:
just build::test-host
just build::build-firmware-test-debug

# 5. На хосте:
just host::flash-test-debug
just host::hil-run
```

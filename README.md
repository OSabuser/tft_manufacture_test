# tft_manufacture_test

Монорепозиторий для **MIMXRT1052CVJ5B**. Содержит три независимых firmware-проекта
с общей инфраструктурой сборки, тестирования и инструментарием.

> Архитектура рабочего окружения — [docs/DEV_ARCH.md](docs/DEV_ARCH.md)

---

## Firmware-проекты

| Проект              | Путь                   | Описание                                                                               |
| ------------------- | ---------------------- | -------------------------------------------------------------------------------------- |
| Тестовая прошивка (✅ реализована) | `firmware/test/`       | Входной контроль платы: CAN, UART, SDRAM, QSPI, SDIO, RGB, оптовходы, LED, кнопки, MQS |
| Загрузчик (⏳ запланирован) | `firmware/bootloader/` | A/B обновление через uSD. Обновляется только через USB ROM + blhost / SWD              |
| Production прошивка (⏳ запланирован) | `firmware/tft_app/`    | Приложение с реализацией логики лифтового индикатора. Обновляется загрузчиком          |

`bootloader`/`tft_app` пока не реализованы.

---

## Инструменты (`tools/`)

| Инструмент         | Путь                | Назначение                                                                                                   |
| ------------------ | ------------------- | -------------------------------------------------------------------------------------------------------------- |
| Сервисный TUI      | `tools/production/` | Диагностика и прошивка готовых плат сервисным инженером (Textual, standalone-бинарь). [README](tools/production/README.md) |
| Прошивка (dev-CLI) | `tools/host/`       | USB SDP / SWD прошивка при разработке (`sdphost`/`blhost`/`nxpimage`/`pyOCD`). [README](tools/host/README.md) |
| HIL-тесты          | `tools/hil/`        | pytest-окружение аппаратных тестов (pyOCD + M5StampPLC). [README](tools/hil/README.md)                        |

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

---

## Зависимости

|                                               | Подход               |
| --------------------------------------------- | -------------------- |
| NXP MCUXpresso SDK, FreeRTOS, FatFS, LittleFS | vendored             |
| Unity, fff, SEGGER RTT                        | vendored             |
| pyOCD, pyserial, pytest, mpremote             | `tools/hil/uv.lock`  |
| spsdk (nxpimage, blhost, sdphost — dev-CLI)   | `tools/host/uv.lock` |
| spsdk (McuBoot/SDP/HabImage — прямой Python API), Textual | `tools/production/uv.lock` |

Всё что не меняется — vendored. Сборка работает после `git clone` без интернета
(кроме Python-зависимостей).

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

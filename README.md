# tft_manufacture_test

Монорепозиторий для **MIMXRT1052CVJ5B**. Содержит три независимых firmware-проекта с общей инфраструктурой сборки, тестирования и инструментарием.

> Архитектура рабочего окружения — [docs/DEV_ARCH.md](docs/DEV_ARCH.md)

---

## Три firmware-проекта

| Проект | Путь | Описание |
|--------|------|----------|
| Тестовая прошивка | `firmware/test/` | Входной контроль платы: CAN, UART, SDRAM, QSPI, SDIO, RGB, оптовходы, LED, кнопки, MQS |
| Загрузчик | `firmware/bootloader/` | A/B обновление через uSD. Сам обновляется только через USB ROM + blhost |
| Боевая прошивка | `firmware/tft_app/` | FreeRTOS + FatFS + бизнес-логика. Обновляется загрузчиком |

---

## BSP

| Модуль | Путь | Описание |
|--------|------|----------|
| `bsp_led` | `bsp/led/` | Два UserLed (GPIO3[3], GPIO3[4]) |
| `bsp_tick` | `bsp/tick/` | SysTick / FreeRTOS-совместимый таймер |
| `bsp_uart_host` | `bsp/uart_host/` | LPUART1 — MCU-Link VCOM (J2) |
| `bsp_opto` | `bsp/opto/` | Оптоизолированные входы PS2801-4: EXT_IN1, EXT_IN2, RS_RX |
| `bsp_usb_cdc` | `bsp/usb_cdc/` | USB CDC ACM |
| generated | `bsp/generated/` | NXP Config Tools: pin_mux, clock_config, board, startup |

---

## Тестирование

| Уровень | Где | Инструменты | Запуск |
|---------|-----|-------------|--------|
| Host unit-тесты | `tests/host/` | Unity + fff, clang | `just build::test-host` (devcontainer) |
| HIL target-тесты | `tests/target/` + `tools/hil/` | pyOCD + pyserial + pytest + M5StampPLC | `just host::hil-run` (хост) |

**Host-тесты** запускаются в devcontainer без железа. BSP-модули тестируются через fff-фейки и stub-хедеры.

**HIL-тесты** — каждый тест это пара: C-прошивка с UART CLI (`tests/target/<n>/`) и pytest-файл (`tools/hil/test_<n>.py`). pyOCD загружает ELF в RAM через MCU-Link. Тесты с внешними сигналами управляются через M5StampPLC (реле → оптовходы таргета).

```bash
pytest → uart_cmd() → MCU-Link VCOM → RT1052
pytest → m5.opto_set() → M5StampPLC RLY → EXT_IN1/IN2/RS_RX → RT1052
```

- Как добавить host-тест — [tests/HOST_CREATE_TEST.md](tests/HOST_CREATE_TEST.md)
- Как добавить HIL-тест — [docs/testing/hil/HIL_CREATE_TEST.md](docs/testing/hil/HIL_CREATE_TEST.md)
- Как запустить HIL-тесты — [docs/testing/hil/HIL_HOWTO.md](docs/testing/hil/HIL_HOWTO.md)
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

| | Подход |
|--|--------|
| NXP MCUXpresso SDK, FreeRTOS, FatFS, LittleFS | vendored |
| Unity, fff, SEGGER RTT | vendored |
| pyOCD, pyserial, pytest, mpremote | `tools/hil/uv.lock` |
| spsdk (nxpimage, blhost, sdphost) | `tools/host/uv.lock` |

Всё что не меняется — vendored. Сборка работает после `git clone` без интернета (кроме Python-зависимостей).

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

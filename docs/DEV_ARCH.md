# Архитектура рабочего окружения разработчика

> Проект: TFT Firmware (MIMXRT1052CVJ5B)
> Документ описывает рабочий процесс разработчиков: разворачивание окружения,
> сборка, тестирование (host + HIL), отладка и прошивка платы.

---

## 1. Концепция

Рабочее окружение разделено на два контекста с чёткой границей:

**Devcontainer** — всё что касается кода: сборка, статический анализ,
форматирование, host-тесты, сборка HIL target-прошивок, подготовка HAB-образов.
Управляется через VSCode tasks и модуль `just build::`.

**Хост** — всё что касается железа: прошивка платы через USB, HIL-тесты
через pyOCD + pytest, отладка через GDB-сервер.
Управляется через модуль `just host::`.

---

## 2. Компоненты окружения

```bash
ПК разработчика
│
├── Хост (Linux / macOS / Windows + Git Bash)
│   ├── just          ← запуск задач хостового уровня (just host::*)
│   ├── docker        ← управление devcontainer
│   ├── uv + spsdk    ← прошивка платы (flash_usb.py, sdphost, blhost)
│   │                    venv: tools/host/.venv-host (Linux/macOS)
│   │                          tools/host/.venv-host-win (Windows)
│   ├── uv + pyocd    ← HIL-тесты (tools/hil/.venv)
│   │     + pyserial
│   │     + pytest
│   ├── JLinkGDBServer / probe-rs  ← сервер отладки (USB → TCP :2331)
│   └── VSCode        ← IDE (Dev Containers extension)
│
├── Devcontainer (Docker)
│   ├── ARM GCC 13.3     ← кросс-компилятор (firmware + HIL target-прошивки)
│   ├── cmake + ninja    ← система сборки
│   ├── clang-17         ← компилятор для host-тестов
│   ├── clangd-17        ← LSP (автодополнение, диагностика)
│   ├── clang-tidy-17    ← статический анализ
│   ├── clang-format-17  ← форматирование кода
│   ├── just             ← запуск задач внутри контейнера (just build::*)
│   ├── uv + spsdk       ← сборка HAB-образов (только nxpimage)
│   └── Unity + fff      ← фреймворки host-тестов
│
└── Плата TFT (IMXRT1052)
    ├── USB ──────────────────▶ хост (SDP-режим, прошивка)
    ├── NXP MCU-Link (USB) ───▶ хост (CMSIS-DAP SWD + VCOM)
    │     ├── SWD  ← pyOCD загружает HIL ELF в RAM
    │     └── VCOM ← pytest общается с прошивкой через UART
    ├── SWD ──────────────────▶ JLink/probe-rs на хосте (отладка)
    └── CAN / UART / IO ──────▶ локальный стенд (будущие HIL-тесты)
```

---

## 3. Что устанавливается и где

| Инструмент | Хост | Devcontainer | Сервер |
|------------|------|--------------|--------|
| `just` | ✅ | ✅ Dockerfile | ✅ |
| `docker` | ✅ | — | — |
| `uv` | ✅ | ✅ Dockerfile | ✅ |
| `spsdk` | ✅ uv sync | ✅ uv sync | ✅ uv sync |
| `pyocd` + `pyserial` + `pytest` | ✅ uv sync | — | ✅ uv sync |
| ARM GCC toolchain | — | ✅ | — |
| `cmake` / `ninja` | — | ✅ | — |
| `clang` / `clangd` | — | ✅ | — |
| Unity / fff | — | ✅ | — |
| JLink / probe-rs | ✅ | — | — |

`spsdk` и `pyocd` — разные venv с разными ролями:

- `tools/host/` — `spsdk`: `nxpimage` + `sdphost` + `blhost` — прошивка через USB ROM
- `tools/hil/` — `pyocd` + `pyserial` + `pytest` — HIL-тесты через SWD + VCOM

---

## 3.1 Конфигурация проекта — `.env`

`.env` в корне репозитория — единый источник конфигурации для всего стека.

```ini
# USB VID:PID (NXP)
BOOTROM_VID=1fc9
BOOTROM_PID=0130
FLASHLOADER_VID=15a2
FLASHLOADER_PID=0073

# GDB / отладка
GDB_PORT=3333
GDB_EXECUTABLE=gdb-multiarch
OPENOCD_INTERFACE=cmsis-dap.cfg
TARGET_CFG=target/imxrt.cfg

# HIL — аппаратный стенд
HIL_VCOM_PORT=/dev/tty.usbmodemXXXX   # VCOM-порт MCU-Link (macOS/Linux)
# HIL_VCOM_BAUD=115200                 # default: 115200
# HIL_READY_TIMEOUT=5.0               # default: 5.0 сек
# HIL_PYOCD_FREQUENCY=1000000         # default: 1 МГц
```

**Как значения попадают в инструменты:**

```bash
.env
 │
 ├─▶ just (set dotenv-load + set export)
 │     ├─▶ just-рецепты: {{BOOTROM_VID}}, {{HIL_VCOM_PORT}}
 │     └─▶ uv run python ← наследует os.environ автоматически
 │           ├─▶ flash_usb.py:   os.environ.get("BOOTROM_VID")
 │           └─▶ env_config.py:  os.environ.get("HIL_VCOM_PORT")
 │
 └─▶ .vscode/launch.json  ← через ${env:GDB_PORT}
```

`.env.example` — шаблон без значений, коммитится в репозиторий.

---

## 4. Структура automation-части репозитория

```bash
/
├── justfile                      ← корневой оркестратор; модули: build, host, ci
├── bootstrap.sh                  ← уровень 0: устанавливает just+uv → just host::bootstrap
├── pyocd.yaml                    ← конфигурация pyOCD (target: cortex_m, RAM-режим)
├── .env / .env.example
├── just/
│   ├── build.just                ← devcontainer: сборка firmware, host-тесты,
│   │                                HAB-образы, HIL target-прошивки
│   ├── host.just                 ← хост: прошивка, bootstrap, HIL-тесты
│   └── ci.just
├── .devcontainer/
│   ├── Dockerfile
│   └── devcontainer.json
├── .vscode/
│   └── tasks.json                ← UI для just build::* (внутри devcontainer)
├── tools/
│   ├── host/                     ← spsdk-окружение (прошивка через USB ROM)
│   │   ├── flash_usb.py
│   │   ├── hab/                  ← HAB yaml-конфиги
│   │   ├── dcd/                  ← ivt_flashloader.bin, dcd.bin
│   │   └── uv.lock
│   └── hil/                      ← HIL pytest-окружение
│       ├── conftest.py           ← фикстуры: loaded_<n>, uart
│       ├── pyocd_utils.py        ← FLEXRAM, ELF loader, run_from_vectors
│       ├── env_config.py         ← конфигурация из os.environ
│       ├── load_and_run.py       ← CLI-утилита загрузки ELF
│       ├── test_uart.py          ← HIL тесты bsp_uart_host
│       └── uv.lock
├── CMakePresets.json             ← Debug · Release · host-debug · target-debug
└── tests/
    ├── host/                     ← host unit-тесты (Unity + fff)
    └── target/                   ← HIL target-прошивки (RAM, pyOCD)
        └── host_uart/            ← CLI для тестирования UART
```

---

## 5. Первый запуск: разворачивание окружения

### 5.1 Предварительные требования

| Платформа | Что нужно до bootstrap |
|-----------|------------------------|
| Linux | `docker`, `git`, `curl` |
| macOS | Docker Desktop, `git` (Xcode CLT) |
| Windows | Docker Desktop, Git for Windows → **Git Bash** |

### 5.2 Единственная команда

```bash
git clone <repo-url> && cd <repo>
./bootstrap.sh
```

### 5.3 Что делает bootstrap

```bash
bootstrap.sh  (уровень 0)
│
├── определить платформу (Linux / macOS / Windows Git Bash)
├── проверить/установить just >= 1.36.0
├── проверить/установить uv >= 0.4.0
│
└── exec just host::bootstrap
      ├── [1/3] check-deps    — just · uv · docker
      ├── [2/3] setup-udev    — udev-правила NXP USB (только Linux)
      │           1FC9:0130 ← BootROM SDP
      │           15A2:0073 ← Flashloader
      └── [3/3] setup-tools   — uv sync в tools/host/
                  SHA-256 uv.lock кешируется → повторный вызов мгновенный
```

### 5.4 Настройка HIL-окружения (один раз, на хосте)

```bash
cd tools/hil
uv sync              # установить pyocd, pyserial, pytest

# Прописать VCOM-порт в .env
# macOS: ls /dev/tty.usbmodem*
# Linux: ls /dev/ttyACM*
# Добавить в .env: HIL_VCOM_PORT=/dev/tty.usbmodemXXXX
```

### 5.5 После bootstrap

```bash
# Открыть VSCode → "Reopen in Container"
# postCreateCommand выполняется автоматически:
#   uv sync (tools/host) + cmake --preset host-debug + cmake --preset Debug
```

---

## 6. Прошивки, boot-стратегии и матрица сборки

### 6.1 Четыре типа сборки

| Пресет | Toolchain | Назначение | Линкер-скрипт |
|--------|-----------|------------|---------------|
| `Debug` / `Release` | ARM GCC | firmware_test, bootloader, tft_app | `flexspi_nor.ld` |
| `host-debug` / `host-release` | clang (host) | Unity + fff тесты | — |
| `target-debug` | ARM GCC | HIL target-прошивки | `ram.ld` |

### 6.2 CMake пресеты

```bash
configurePresets:
  Debug · Release              ← firmware (XIP из Flash)
  host-debug · host-release    ← host unit-тесты
  target-debug                 ← HIL target-прошивки (RAM)

buildPresets (ARM firmware):
  firmware-test-debug / release
  bootloader-debug / release
  app-debug / release
  all-debug / all-release

buildPresets (host-тесты):
  host-debug-build / host-release-build

buildPresets (HIL):
  target-debug-build           ← test_host_uart (и будущие HIL-прошивки)
```

### 6.3 Boot-стратегии

| Прошивка | Стратегия | DCD | Инструмент загрузки |
|----------|-----------|-----|---------------------|
| `firmware_test` | XIP из Flash | ✅ | SPSDK → Flash |
| `bootloader` | Копирование в ITCM | ❌ | SPSDK → Flash |
| `tft_app` | XIP + буферы в SDRAM | ✅ | SPSDK → Flash |
| HIL target (`tests/target/`) | Исполнение из ITCM/DTCM | ❌ | pyOCD → RAM |

**HIL boot-стратегия (`bsp_boot_ram`):** pyOCD настраивает FLEXRAM (128KB ITCM + 128KB DTCM), записывает сегменты ELF по физическим адресам, устанавливает SP/PC из таблицы векторов и запускает выполнение. Flash не используется.

---

## 7. Рабочий процесс разработчика

### 7.1 Карта задач по контекстам

| Задача | Где |
|--------|-----|
| Написание кода, clangd, форматирование | devcontainer |
| Статический анализ (clang-tidy) | devcontainer |
| Host unit-тесты (Unity + fff) | devcontainer |
| Сборка ARM firmware (ELF) | devcontainer |
| Сборка HIL target-прошивок | devcontainer |
| Подготовка HAB-образов (nxpimage) | devcontainer |
| Прошивка платы через USB ROM | **хост** |
| HIL-тесты (pyOCD + pytest) | **хост** |
| Отладка — GDB-сервер (JLink) | **хост** |
| Отладка — GDB-клиент | devcontainer → хост по TCP |

### 7.2 Типичная сессия разработки

```bash
# Devcontainer (терминал VSCode)
just build::test-host                    # host unit-тесты — зелёные?
just build::build-firmware-test-debug    # ELF собирается?
just build::hab-firmware-test-debug      # HAB-образ готов

# Хостовый терминал — прошивка
just flash                               # прошить firmware_test debug во Flash

# Хостовый терминал — HIL
just build::build-hil                    # собрать HIL target-прошивку
# (можно делать в devcontainer)
just host::hil-run                       # загрузить ELF → запустить pytest

# Отладка
# F5 в VSCode → запустить JLinkGDBServer на хосте → attach через cortex-debug
```

### 7.3 VSCode Tasks (внутри devcontainer)

| Таск | Команда |
|------|---------|
| 🔨 Build | `just build::build-<project>-<type>` |
| 🧪 Host Tests (Debug) | `just build::test-host` |
| 🧪 Host Tests (Release) | `just build::test-host-release` |
| 🎯 Build HIL Target Tests | `just build::build-hil` |
| 📦 HAB Image | `just build::hab-<project>-<type>` |
| 📦 HAB All (Debug/Release) | `just build::hab-all-debug/release` |
| 🗑️ Clean | `just build::clean` |

---

## 8. Прошивка платы (хост)

### Перевод в SDP-режим

```bash
1. BOOT_MOD_1 → 3V3
2. Reset
3. Подключить USB → плата как VID:PID 1FC9:0130
4. just host::flash <project> <type>
5. После прошивки: BOOT_MOD_1 → GND → Reset
```

### Команды прошивки

```bash
# project × type
just host::flash firmware_test debug
just host::flash firmware_test release
just host::flash bootloader release
just host::flash tft_app release

# В RAM — быстро, без износа Flash
just host::flash-ram firmware_test

# Псевдонимы
just flash                       # = firmware_test debug → Flash
just host::flash-production      # bootloader + tft_app release
```

---

## 9. HIL-тесты (хост)

HIL-тесты проверяют периферию на реальном железе. MCU-Link обеспечивает два канала по одному USB: SWD (прошивка через pyOCD) и VCOM (UART CLI).

```bash
pytest → uart_cmd("PING\r\n")
  ↓ pyserial / VCOM
MCU-Link
  ↓ LPUART1
RT1052 (HIL прошивка)
  → "PONG\r\n"
```

### Команды

```bash
# Сборка HIL target-прошивок (devcontainer)
just build::build-hil

# Запуск тестов (хост)
just host::hil-run          # загрузить ELF + все тесты
just host::hil-smoke        # только smoke-тесты (-m smoke)
just host::hil-run-fast     # тесты без перезагрузки ELF (--no-load)
just host::hil-load         # только загрузить ELF (без pytest)
```

### Архитектура HIL-теста

Каждый HIL-тест — пара: C-прошивка в `tests/target/<n>/` и pytest-файл в `tools/hil/test_<n>.py`. Прошивка реализует текстовый CLI через `bsp_uart_host`. pytest управляет через `uart_cmd()`.

Протокол готовности: прошивка шлёт `READY\r\n` в цикле пока хост не откроет порт — исключает race condition между загрузкой ELF и открытием COM-порта.

Гайд по добавлению нового HIL-теста — [tests/HIL_CREATE_TEST.md](../tests/HIL_CREATE_TEST.md).

---

## 10. Тестирование

### 10.1 Host unit-тесты

```bash
Фреймворк:  Unity + fff
Пресеты:    host-debug / host-release
Компилятор: clang-17 (не ARM GCC)
Запуск:     just build::test-host
```

BSP-модули тестируются через fff-фейки и stub-хедеры в `tests/host/mocks/`. `BUILD_TESTS_HOST=ON` отключает ARM-специфику.

Гайд — [tests/HOST_CREATE_TEST.md](../tests/HOST_CREATE_TEST.md).

### 10.2 HIL target-тесты

```bash
Инструменты: pyOCD (SWD) + pyserial (UART) + pytest
Пресет:      target-debug  →  ram.ld  →  ITCM/DTCM
Запуск:      just host::hil-run
```

Текущие тесты: `test_uart.py` — PING/ECHO/BUF_SIZE через `bsp_uart_host`.

### 10.3 Будущие HIL-тесты (с M5StampPLC)

Для тестов с внешними интерфейсами (CAN, GPIO, RS485) понадобится промежуточное звено:

```bash
pytest → M5StampPLC (Arduino CLI) → CAN/GPIO → RT1052
```

M5StampPLC реализует тот же текстовый CLI — pytest работает одинаково с обоими каналами.

---

## 11. Отладка

```bash
Хост
├── JLinkGDBServer -device MIMXRT1052 -if SWD -port 2331
│     USB/SWD → плата
└── host.docker.internal:2331  ← доступен из devcontainer

Devcontainer
└── arm-none-eabi-gdb
      target remote host.docker.internal:2331
```

`.vscode/launch.json` (cortex-debug, тип `external`):

```json
{
    "type": "cortex-debug",
    "servertype": "external",
    "gdbTarget": "host.docker.internal:2331",
    "executable": "${workspaceFolder}/build/Debug/firmware/test/firmware_test.elf"
}
```

---

## 12. Жизненный цикл изменений

```bash
feature-ветка
  │
  ├── devcontainer
  │     just build::test-host               ← зелёные host-тесты?
  │     just build::build-firmware-test-debug
  │     just build::build-hil               ← HIL-прошивки собираются?
  │
  ├── хост
  │     just flash                          ← прошить, проверить на железе
  │     just host::hil-run                  ← HIL зелёные?
  │
  ├── подготовка к MR
  │     just build::hab-all-release
  │     just host::flash firmware_test release
  │
  └── Merge Request → GitLab CI
        host-тесты · сборка · HIL · публикация артефактов
              ↓
        Производственный сервер
        just host::incoming   → firmware_test release → HIL
        just host::production → bootloader + tft_app release
```

---

## 13. Производственный сервер

Сервер работает только с готовыми проверенными артефактами. Никакой сборки.

```bash
Сценарий входного контроля:
  just host::incoming
    └── flash firmware_test release → HIL-тесты

Финальная прошивка:
  just host::production
    ├── flash bootloader release
    └── flash tft_app release

Установлено:     just · uv + spsdk · uv + pyocd/pytest · git
НЕ установлено:  docker · cmake · компилятор · ARM toolchain
```

---

## Приложение А: минимальные версии

| Инструмент | Версия | Причина |
|------------|--------|---------|
| `just` | 1.36.0 | поддержка `mod` с кастомным путём |
| `uv` | 0.4.0 | стабильный lockfile формат |
| `docker` | 24.0.0 | Compose v2 |
| `spsdk` | 3.7.x | совместимость с HAB yaml-форматом |
| `pyocd` | 0.36+ | cortex_m target, write_core_register API |
| ARM GCC | 13.3.rel1 | C11, текущий SDK |
| clang/clangd | 17 | поддержка `If:` в `.clangd` |

---

## Приложение Б: шпаргалка

```bash
# ── Первый запуск ───────────────────────────────────────────
./bootstrap.sh
cd tools/hil && uv sync
# VSCode → Reopen in Container

# ── devcontainer ────────────────────────────────────────────
just build::test-host                    # host unit-тесты
just build::build-firmware-test-debug    # сборка firmware
just build::build-hil                    # сборка HIL target-прошивок
just build::hab-firmware-test-debug      # HAB-образ Debug
just build::hab-all-release              # все HAB Release
just build::clean                        # очистить build/

# ── хост — прошивка ─────────────────────────────────────────
just flash                               # firmware_test debug → Flash
just host::flash firmware_test release
just host::flash bootloader release
just host::flash-production              # bootloader + tft_app release
just host::flash-ram firmware_test       # в RAM (без износа Flash)

# ── хост — HIL-тесты ────────────────────────────────────────
just host::hil-run                       # загрузить ELF + все тесты
just host::hil-smoke                     # только smoke
just host::hil-run-fast                  # без перезагрузки ELF
just host::hil-load                      # только загрузить ELF

# ── хост — обслуживание ─────────────────────────────────────
just host::check-deps                    # проверить версии
just host::setup-tools                   # uv sync после git pull
just host::scan                          # найти NXP USB-устройства
just host::upgrade-tools 3.8.0           # обновить spsdk
```

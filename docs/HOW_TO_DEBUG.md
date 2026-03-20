# Отладка прошивок через SWD + GDB

## Обзор архитектуры

Отладка построена на проброске GDB-сервера с хоста в devcontainer по TCP. Это позволяет держать весь инструментарий сборки и языковой сервер внутри контейнера, не проводя USB-пробник внутрь Docker.

```
┌─────────────────────────────────────┐     ┌──────────────────────────────────┐
│            Хост (macOS/Linux)       │     │         DevContainer             │
│                                     │     │                                  │
│  just host::debug-server            │     │  VSCode + cortex-debug           │
│  └─ pyocd gdbserver :3333 ──────────┼─────┼──► arm-none-eabi-gdb             │
│                                     │TCP  │       └─ символы из .elf         │
│  MCU-Link (CMSIS-DAP)               │3333 │                                  │
│  └─ SWD ──► MIMXRT1052             │     │  RTT Console (SEGGER RTT логи)   │
│             Flash / SDRAM           │     │  Peripherals (SVD регистры)      │
│             SEGGER RTT буфер        │     │  RTOS view (FreeRTOS задачи)     │
└─────────────────────────────────────┘     └──────────────────────────────────┘
```

**Ключевой принцип:** `pyocd gdbserver` запускается на хосте и слушает на `0.0.0.0:3333`. Из контейнера GDB подключается через `host.docker.internal:3333` — специальный DNS-алиас Docker, который резолвится в IP хост-машины.

---

## Компоненты

### На хосте

| Компонент | Роль | Источник |
|---|---|---|
| `pyocd` | GDB-сервер + flash-программатор | `tools/hil/uv.lock` |
| `MCU-Link` | CMSIS-DAP v2 пробник | USB к плате |
| `just host::debug-server` | Запуск GDB-сервера | `just/host.just` |
| `just host::flash-swd-*` | Прошивка через SWD | `just/host.just` |
| `tools/host/flash_swd.py` | Сборка FCB+HAB образа и запись | `tools/host/` |
| `tools/host/dcd/w25q128_fdcb.bin` | FCB для W25Q128 (Quad SPI) | NXP SecureProvisioningTool |

### В devcontainer

| Компонент | Роль |
|---|---|
| `arm-none-eabi-gdb` | GDB клиент, подключается к серверу на хосте |
| `cortex-debug` (VSCode extension) | UI для GDB: брейкпоинты, стек, регистры |
| `.vscode/launch.json` | Конфигурации запуска отладки |
| `.vscode/tasks.json` | `preLaunchTask` — пересборка ELF перед стартом |
| `build/Debug/*.elf` | Символы для GDB (DWARF debug info) |
| `bsp/generated/startup/MIMXRT1052.xml` | SVD — описание регистров периферии |

### Конфигурация

Параметры отладки задаются в `.env` и автоматически экспортируются через `just` (`set export`), откуда наследуются скриптами:

```bash
# .env — секция Debug / SWD
GDB_PORT=3333
PYOCD_TARGET=mimxrt1050_quadspi
PYOCD_FREQUENCY=4000000
FCB_PATH=tools/host/dcd/w25q128_fdcb.bin
```

---

## Прошивки, поддерживаемые отладкой

| Конфигурация VSCode | ELF | Особенности |
|---|---|---|
| `🐛 Debug: firmware_test` | `build/Debug/firmware_test.elf` | Bare-metal, входной контроль |
| `🐛 Debug: bootloader` | `build/Debug/bootloader.elf` | Bare-metal, A/B обновление |
| `🐛 Debug: tft_app (FreeRTOS)` | `build/Debug/app.elf` | FreeRTOS, task view |

Все три — XIP-прошивки, исполняются напрямую из QuadSPI NOR Flash (`0x60000000`).

---

## Режимы запуска отладки

### Режим А — прошивка уже в Flash

Стандартный ежедневный сценарий. Прошивка была залита ранее любым способом и исполняется на плате.

```bash
# 1. Хост — запустить GDB-сервер (оставить работать в отдельном терминале)
just host::debug-server

# 2. DevContainer — VSCode
#    Run & Debug (Ctrl+Shift+D) → выбрать конфигурацию → F5
```

GDB сбрасывает MCU, загружает символы из ELF и останавливается на входе в `main`. Flash не перезаписывается.

### Режим Б — прошить через SWD, затем отладить

Когда нужно обновить прошивку без перевода платы в режим Serial Downloader. Удобно при итеративной разработке когда плата закреплена в стенде.

```bash
# 1. DevContainer — собрать HAB-образ
just build::hab-firmware-test-debug

# 2. Хост — прошить через SWD (MCU-Link, без смены BOOT_MODE)
just host::flash-swd-test-debug

# 3. ⚡ Power cycle платы (обязательно — VECTRESET не реинициализирует FlexSPI)

# 4. Хост — запустить GDB-сервер
just host::debug-server

# 5. DevContainer — VSCode → 🐛 Debug: firmware_test → F5
```

### Режим В — прошить через USB SDP, затем отладить

Классический способ. Требует перевода платы в режим Serial Downloader (BOOT_MODE = 01).

```bash
# 1. DevContainer — собрать
just build::build-firmware-test-debug

# 2. Хост — перевести плату в Serial Downloader mode, затем:
just host::flash-test-debug

# 3. Хост — запустить GDB-сервер
just host::debug-server

# 4. DevContainer — VSCode → 🐛 Debug: firmware_test → F5
```

---

## Почему flash через SWD требует FCB

При прошивке через USB SDP (режимы А и В) ROM-загрузчик сам инициализирует FlexSPI контроллер по DCD из HAB-образа — Flash Configuration Block ему не нужен.

При прошивке через SWD flash-алгоритм pyOCD записывает данные напрямую в NOR Flash. При cold-start Boot ROM первым делом читает FCB по адресу `0x60000000`, конфигурирует по нему FlexSPI, и только потом ищет IVT. Без FCB бутлоадер не может обратиться к Flash.

`flash_swd.py` решает это, собирая итоговый образ перед записью:

```bash
0x60000000  w25q128_fdcb.bin  (512 байт)  — FCB: параметры W25Q128, Quad SPI
0x60000200  0xFF × 3584 байт             — padding (значение стёртой ячейки)
0x60001000  firmware_test_hab.bin         — IVT + DCD + код (ivtOffset = 0x1000)
```

Весь диапазон `0x60000000–0x6000FFFF` умещается в один 64KB-сектор Flash, поэтому стирается и записывается за одну транзакцию — FCB и HAB не перезаписывают друг друга.

---

## RTT-логи

SEGGER RTT включён только в Debug-сборках (`SEGGER_RTT_ENABLED=ON` в `CMakePresets.json`). В Release-сборках RTT отключён и символ `_SEGGER_RTT` в ELF отсутствует.

После старта отладки вкладка `TERMINAL → RTT` в VSCode принимает вывод из RTT-буфера канала 0. `cortex-debug` находит адрес буфера автоматически по символу `_SEGGER_RTT` из ELF (`address: auto` в `launch.json`).

Использование в коде:

```c
#include "SEGGER_RTT.h"

SEGGER_RTT_printf(0, "value = %d\n", value);
```

---

## FreeRTOS task view

Конфигурация `🐛 Debug: tft_app (FreeRTOS)` включает `"rtos": "FreeRTOS"` — cortex-debug разбирает внутренние структуры планировщика и показывает вкладку `RTOS` с таблицей задач: имя, состояние (`Running` / `Ready` / `Blocked` / `Suspended`), использование стека, приоритет. При паузе можно переключиться в контекст любой задачи и просмотреть её стек вызовов.

---

## Просмотр регистров периферии

Вкладка `Peripherals` в панели отладки показывает все периферийные блоки MIMXRT1052 по SVD-файлу `bsp/generated/startup/MIMXRT1052.xml`. Значения регистров обновляются при каждой паузе. Можно раскрыть любой блок (GPIO, LPUART, USB, FlexSPI и т.д.) и просматривать поля побитово.

---

## Ограничения и важные замечания

**MCU-Link монопольный ресурс.** `debug-server` и `flash-swd` не могут работать одновременно — оба занимают пробник. Перед `flash-swd` остановите сервер (Ctrl+C), и наоборот.

**HIL-тесты vs отладка.** pyOCD также используется для HIL (загрузка ELF в RAM через `pyocd.yaml`). Перед запуском HIL-тестов (`just host::hil-run`) остановите GDB-сервер.

**Power cycle после flash-swd обязателен.** pyOCD завершает запись командой VECTRESET, которая не реинициализирует FlexSPI контроллер. Boot ROM при таком сбросе не может прочитать FCB и не стартует из Flash. Только полное отключение питания гарантирует корректный cold-start.

**Только Debug-сборки.** Отладка с символами возможна только для `Debug` CMake-пресета. Release-сборки компилируются с `-O2` без DWARF-символов.

---

## Быстрый старт (первый запуск)

```bash
# 1. Убедиться что cortex-debug установлен в devcontainer
#    .devcontainer/devcontainer.json → extensions: ["marus25.cortex-debug"]

# 2. Убедиться что в .devcontainer/devcontainer.json есть (для Linux-хостов):
#    "runArgs": ["--add-host=host.docker.internal:host-gateway"]

# 3. Залить прошивку любым способом (один раз)
just host::flash-test-debug          # USB SDP
# или
just host::flash-swd-test-debug      # SWD (после just build::hab-firmware-test-debug)

# 4. Запустить GDB-сервер на хосте
just host::debug-server

# 5. В VSCode (devcontainer)
#    Ctrl+Shift+D → 🐛 Debug: firmware_test → F5
```

---

## Дерево файлов отладки

```bash
.
├── .env                                  # GDB_PORT, PYOCD_TARGET, PYOCD_FREQUENCY, FCB_PATH
├── .vscode/
│   ├── launch.json                       # Конфигурации cortex-debug (3 проекта)
│   └── tasks.json                        # preLaunchTask: build:*-debug
├── bsp/generated/startup/
│   └── MIMXRT1052.xml                    # SVD — регистры периферии
├── just/
│   └── host.just                         # debug-server, flash-swd-*
└── tools/
    ├── hil/                              # uv-проект с pyocd
    └── host/
        ├── flash_swd.py                  # FCB + HAB → Flash через pyOCD
        └── dcd/
            └── w25q128_fdcb.bin          # FCB для W25Q128 Quad SPI
```
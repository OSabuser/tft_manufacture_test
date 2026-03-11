# Архитектура рабочего окружения разработчика

> Проект: TFT Firmware (MIMXRT1052CVJ5B)
> Документ описывает итоговый рабочий процесс разработчиков: от разворачивания
> окружения до сборки, тестирования, отладки и прошивки платы.
> Производственный сервер описан кратко — подробно в отдельном документе.

---

## 1. Концепция

Рабочее окружение разделено на два контекста с чёткой границей:

**Devcontainer** — всё что касается кода: сборка, статический анализ,
форматирование, host-тесты, подготовка HAB-образов. Разработчик проводит
здесь большую часть времени. Управляется через VSCode tasks и `scripts/build.just`.

**Хост** — всё что касается железа: прошивка платы через USB, отладка
через JLink/probe-rs. Управляется через корневой `Justfile`.

Такое разделение решает несколько проблем: USB-устройства не требуют
проброса в контейнер; оба разработчика работают в идентичных условиях;
CI использует те же команды что и локальная разработка.

---

## 2. Компоненты окружения

```bash
ПК разработчика
│
├── Хост (Linux / macOS / Windows + Git Bash)
│   ├── just          ← запуск задач хостового уровня
│   ├── docker        ← управление devcontainer
│   ├── git           ← работа с репозиторием
│   ├── uv + spsdk    ← прошивка платы (flash_usb.py, sdphost, blhost)
│   ├── JLinkGDBServer / probe-rs  ← сервер отладки (USB → TCP :2331)
│   └── VSCode        ← IDE (Dev Containers extension)
│
├── Devcontainer (Docker)
│   ├── ARM GCC 13.3     ← кросс-компилятор
│   ├── cmake + ninja    ← система сборки
│   ├── clang-17         ← компилятор для host-тестов
│   ├── clangd-17        ← LSP (автодополнение, диагностика)
│   ├── clang-tidy-17    ← статический анализ
│   ├── clang-format-17  ← форматирование кода
│   ├── cmake-format     ← форматирование CMakeLists
│   ├── just             ← запуск задач внутри контейнера
│   ├── uv + spsdk       ← сборка HAB-образов (только nxpimage)
│   └── Unity + fff      ← фреймворки host-тестов
│
└── Плата TFT (IMXRT1052) — на столе у разработчика
    ├── USB ──────────────▶ хост (SDP-режим, прошивка)
    ├── SWD ──────────────▶ JLink/probe-rs на хосте (отладка)
    └── CAN / UART / IO ──▶ локальный стенд (target-тесты)
```

---

## 3. Что устанавливается и где

| Инструмент | Хост | Devcontainer | Сервер |
|---|---|---|---|
| `just` | ✅ | ✅ Dockerfile | ✅ |
| `docker` | ✅ | — | — |
| `git` | ✅ | ✅ | ✅ |
| `uv` | ✅ | ✅ Dockerfile | ✅ |
| `spsdk` | ✅ uv sync | ✅ uv sync | ✅ uv sync |
| ARM GCC toolchain | — | ✅ | — |
| `cmake` / `ninja` | — | ✅ | — |
| `clang` / `clangd` | — | ✅ | — |
| Unity / fff | — | ✅ | — |
| JLink / probe-rs | ✅ | — | — |

`spsdk` присутствует везде, но с разными ролями:

- **devcontainer** — только `nxpimage` для сборки HAB-образов
- **хост** — полный стек: `nxpimage` + `sdphost` + `blhost` для прошивки
- **сервер** — то же что на хосте, но для производственного сценария

Версия `spsdk` зафиксирована в `tools/host/uv.lock` — все три места
используют одну и ту же версию.

---

## 4. Структура репозитория (automation-часть)

```bash
/
├── Justfile                      ← хост: прошивка, setup, pipeline
├── scripts/
│   └── bootstrap.sh              ← уровень 0: just → just bootstrap
│   └── build.just                ← devcontainer: сборка, тесты, HAB
├── .devcontainer/
│   ├── Dockerfile
│   └── devcontainer.json
├── tools/
│   └── host/
│       ├── hab/
│       │   ├── hab_firmware_test_debug.yaml
│       │   ├── hab_firmware_test_release.yaml
│       │   ├── hab_bootloader_debug.yaml
│       │   ├── hab_bootloader_release.yaml
│       │   ├── hab_app_debug.yaml
│       │   └── hab_app_release.yaml
│       ├── dcd/
│       │   ├── dcd.bin
│       │   └── ivt_flashloader.bin
│       ├── flash_usb.py
│       ├── pyproject.toml
│       └── uv.lock
├── .vscode/
│   └── tasks.json                ← UI для build.just (внутри devcontainer)
├── CMakePresets.json
├── cmake/
├── sdk/
├── bsp/
├── lib/
├── firmware/
│   ├── test/
│   ├── bootloader/
│   └── app/
└── tests/                        ← host-тесты (Unity + fff)
```

---

## 5. Первый запуск: разворачивание окружения

### 5.1 Предварительные требования

| Платформа | Что нужно до bootstrap |
|---|---|
| Linux | `docker`, `git`, `curl` |
| macOS | Docker Desktop, `git` (Xcode CLT) |
| Windows | Docker Desktop, Git for Windows → **использовать Git Bash** |

### 5.2 Единственная команда для нового разработчика

```bash
git clone <repo-url> && cd <repo>
./scripts/bootstrap.sh
```

### 5.3 Что делает bootstrap

```bash
bootstrap.sh  (уровень 0)
│
├── определить платформу (Linux / macOS / Windows Git Bash)
│   uname: MINGW64_NT-... → windows, Linux → linux, Darwin → macos
│
├── проверить just (semver без sort -V — работает в Git Bash)
│   < 1.27.0 или отсутствует:
│     Linux/macOS → curl | bash → ~/.local/bin/just
│     Windows     → winget install --id Casey.Just
│                   (перезапустить Git Bash после установки)
│
└── exec just bootstrap
      │
      ├── [1/3] check-deps
      │         just >= 1.27.0 · uv >= 0.4.0 · docker >= 24.0.0
      │         платформо-зависимые подсказки при ошибках
      │
      ├── [2/3] setup-udev  (только Linux)
      │         /etc/udev/rules.d/99-nxp-mimxrt.rules:
      │           1FC9:0130  ← NXP BootROM (SDP-режим)
      │           15A2:0073  ← NXP Flashloader
      │         usermod -a -G plugdev $USER
      │         требует re-login · на macOS/Windows пропускается
      │
      └── [3/3] setup-tools
                uv sync в tools/host/
                SHA-256 uv.lock кэшируется в .cache/
                повторный вызов мгновенный если lockfile не изменился
```

### 5.4 После bootstrap

```bash
Открыть VSCode → "Reopen in Container"
```

`postCreateCommand` выполняется автоматически при поднятии контейнера:

```bash
cd tools/host && uv sync &&
cd ../.. &&
cmake --preset host-debug &&
cmake --preset Debug
```

Прогрев CMake-кэша нужен чтобы clangd и IntelliSense заработали сразу,
без первой ручной сборки.

---

## 6. Прошивки, boot-стратегии и матрица сборки

### 6.1 Три подпроекта

| Прошивка | Boot-стратегия | DCD | Назначение |
|---|---|---|---|
| `firmware_test` | XIP из Flash | ✅ | Входной контроль, тестирование периферии |
| `bootloader` | Копирование в ITCM | ❌ | Загрузчик, не использует SDRAM |
| `app` | XIP + буферы в SDRAM | ✅ | Основное приложение (FreeRTOS, LCDIF) |

### 6.2 Матрица сборки

Каждый проект собирается в двух режимах:

| | Debug | Release |
|---|---|---|
| `firmware_test` | разработка, отладка | HAB для сервера |
| `bootloader` | отладка загрузчика | финальная прошивка |
| `app` | отладка приложения | финальная прошивка |

### 6.3 CMake пресеты

```bash
configurePresets:  Debug · Release · host-debug · host-release

buildPresets (ARM):
  all-debug / all-release              ← все проекты (для CI)
  firmware-test-debug / release
  bootloader-debug / release
  app-debug / release

buildPresets (host):
  host-debug-build / host-release-build
```

### 6.4 Карта Flash (W25Q128, 16 MB)

```bash
0x60000000  FCB — Flash Config Block       512 байт  (пишет Flashloader)
0x60001000  IVT + BDT                                ← начало HAB-образа
0x60001040  DCD — инициализация SDRAM      ~1088 байт (firmware_test, app)
0x60003000  Код прошивки (.text, .data…)
```

---

## 7. Рабочий процесс разработчика

### 7.1 Карта задач по контекстам

| Задача | Где |
|---|---|
| Написание кода, clangd, форматирование | devcontainer |
| Статический анализ (clang-tidy) | devcontainer |
| Host-тесты (Unity + fff) | devcontainer |
| Сборка ARM firmware (ELF) | devcontainer |
| Подготовка HAB-образов (nxpimage) | devcontainer |
| Прошивка платы через USB | **хост** |
| Отладка — GDB-сервер (JLink/probe-rs) | **хост** |
| Отладка — GDB-клиент | devcontainer → хост по TCP |
| Target-тесты (управление стендом) | **хост** |

### 7.2 Типичная сессия разработки

```
Открыть VSCode → работать в devcontainer весь день
│
├── писать код
│
├── Ctrl+Shift+P → "Run Task" → 🧪 Host Tests (Debug)
│     или в терминале: just --justfile just/build.just test-host
│
├── Ctrl+Shift+P → "Run Task" → 🔨 Build → firmware-test · debug
│     → build/Debug/firmware/test/firmware_test.elf
│
├── Ctrl+Shift+P → "Run Task" → 📦 HAB Image → firmware-test · debug
│     → build/Debug/firmware_test_hab.bin
│
│   Переключиться в хостовый терминал
│
├── just flash firmware_test debug      ← прошить отладочный образ
│   или
├── just flash-ram firmware_test debug  ← загрузить в RAM (быстро, без износа Flash)
│
└── F5 в VSCode → отладка через JLink
```

### 7.3 VSCode Tasks (внутри devcontainer)

| Таск | Input 1 | Input 2 | Команда |
|---|---|---|---|
| 🔨 Build | project | debug/release | `just build-<project>-<type>` |
| 🧪 Host Tests (Debug) | — | — | `just test-host` |
| 🧪 Host Tests (Release) | — | — | `just test-host-release` |
| 📦 HAB Image | project | debug/release | `just hab-<project>-<type>` |
| 📦 HAB All (Debug) | — | — | `just hab-all-debug` |
| 📦 HAB All (Release) | — | — | `just hab-all-release` |
| 🗑️ Clean | — | — | `just clean` |

Таски «Build» и «HAB Image» запрашивают два input последовательно:
сначала проект (`firmware-test / bootloader / app / all`),
затем тип (`debug / release`).

---

## 8. Прошивка платы (хост)

### 8.1 Перевод платы в SDP-режим

```
1. BOOT_MOD_1 → 3V3
2. Reset
3. Подключить USB к хосту
   → плата определяется как VID:PID = 1FC9:0130
4. Выполнить нужный just flash-* рецепт
5. После прошивки: BOOT_MOD_1 → GND, Reset
   → плата стартует из Flash
```

### 8.2 Команды прошивки

```bash
# Основной рецепт (project × type)
just flash firmware_test debug    # разработка — итерации с отладчиком
just flash firmware_test release  # проверить как будет на сервере
just flash bootloader debug
just flash bootloader release
just flash app debug
just flash app release

# TODO: Загрузка в RAM — без записи во Flash, мгновенный старт
# Удобно для частых итераций: не изнашивает Flash, не нужен BOOT_MOD переключатель
just flash-ram firmware_test        # default: debug
just flash-ram firmware_test debug
just flash-ram app release

# TODO: Быстрые псевдонимы
just flash-test-debug      # = just flash firmware_test debug
just flash-test-release    # = just flash firmware_test release
just flash-production      # bootloader release + app release
```

### 8.3 Что происходит внутри flash_usb.py

```
Плата в SDP-режиме (1FC9:0130)
  │
  ├── sdphost: загрузить ivt_flashloader.bin в RAM (0x20001C00)
  └── sdphost: jump-address 0x20001C00
        │
        │  ожидание до 10с — Flashloader поднимается как 15A2:0073
        │
  Flashloader (15A2:0073)
        ├── fill-memory + configure-memory (0xC0000007) — инициализация FlexSPI
        ├── flash-erase-region 0x60000000
        ├── fill-memory + configure-memory (0xF000000F) — запись FCB
        ├── write-memory 0x60001000 ← HAB-образ
        └── reset
```

---

## 9. Отладка

JLink/probe-rs работает на хосте напрямую через USB/SWD.
GDB-клиент в devcontainer подключается к серверу по TCP.

```bash
Хост
├── JLinkGDBServer -device MIMXRT1052 -if SWD -port 2331
│     USB/SWD → плата
└── host.docker.internal:2331 ← доступен из devcontainer

Devcontainer
└── arm-none-eabi-gdb / probe-rs
      target remote host.docker.internal:2331
```

`.vscode/launch.json`:

```json
{
    "type": "cortex-debug",
    "servertype": "external",
    "gdbTarget": "host.docker.internal:2331",
    "executable": "${workspaceFolder}/build/Debug/firmware/test/firmware_test.elf"
}
```

`host.docker.internal` — стандартный DNS-алиас Docker для хоста.
Работает на macOS, Windows и Linux (Docker Desktop).

> USB-passthrough программатора в Docker на macOS невозможен, на других
> платформах нестабилен. TCP-мост — единственное надёжное решение для
> всех платформ.

---

## 10. Тесты

### 10.1 Host-тесты

Выполняются в devcontainer на хостовом компиляторе (x86/arm64). Железо не нужно.

```bash
Фреймворк:  Unity + fff
Пресеты:    host-debug / host-release
Компилятор: системный clang-17 (не ARM GCC)
Запуск:     just --justfile just/build.just test-host
Результат:  JUnit XML → VSCode CTest Lab + GitLab CI
```

`BUILD_TESTS_HOST=ON` отключает ARM-специфику (BSP, SDK) — компилируются
только тестируемые модули и fff-заглушки.

### 10.2 Target-тесты

Выполняются на физической плате. Прошивается `firmware_test`,
стенд подаёт сигналы и проверяет ответы.

```bash
Стенд (M5StampPLC или аналог) ←→ Плата TFT

Тестируемые подсистемы:
  SDRAM 32 MB        — чтение/запись паттернов
  QSPI Flash         — erase / write / verify
  CAN                — loopback + внешний фрейм от стенда
  UART TTL           — echo-тест
  UART +24V изол.    — echo-тест
  Гальв. входы +24V  — состояния при подаче напряжения от стенда
  RTC BM8563         — установка / чтение времени
  SD-карта (SDIO)    — монтирование, R/W файл
  MQS (аудио)        — воспроизведение тестового сигнала
  IR-приёмник        — приём тестового кода от стенда
```

---

## 11. Жизненный цикл изменений

```bash
feature-ветка
  │
  ├── код в devcontainer
  │     🧪 Host Tests         ← зелёные?
  │     🔨 Build firmware-test debug  ← компилируется?
  │
  ├── проверка на железе
  │     📦 HAB Image → firmware-test · debug
  │     just flash-test-debug          ← прошить
  │     target-тесты со стендом        ← периферия работает?
  │
  ├── подготовка к MR
  │     📦 HAB All (Release)           ← финальные образы
  │     just flash firmware_test release  ← убедиться что release работает
  │
  └── Merge Request → GitLab
        [CI pipeline — отдельная тема]
        host-тесты, сборка, публикация артефактов
              │
              ▼
        Производственный сервер
        just incoming   → firmware_test release → HIL
        just production → bootloader + app release
```

---

## 12. Обновление зависимостей

### spsdk (на хосте разработчика)

```bash
just upgrade-tools 3.8.0
git add tools/host/uv.lock tools/host/pyproject.toml
git commit -m "chore: upgrade spsdk to 3.8.0"
```

После этого у всех разработчиков и в контейнере обновится автоматически
при следующем `just setup-tools` / `uv sync`.

### just в Dockerfile

```dockerfile
ARG JUST_VERSION=1.40.0   # .devcontainer/Dockerfile — единственное место
```

### ARM toolchain

```dockerfile
ARG TOOLCHAIN_VERSION=14.2.rel1   # .devcontainer/Dockerfile
```

Пересборка инвалидирует только Stage 1 (toolchain). Stage 2
(clang, cmake и др.) берётся из кэша — пересборка быстрая.

### После git pull если изменился uv.lock

```bash
just setup-tools   # автоматически обнаружит изменение и выполнит uv sync
```

---

## 13. Производственный сервер (к сведению)

Сервер работает **только с готовыми проверенными артефактами**.
Никакой сборки, никакого компилятора.

```bash
Источник: GitLab Releases или FTP
  — только теггированные релизы, прошедшие CI

Сценарий входного контроля новой платы:
  1. just incoming
       └── flash firmware_test release → HIL-тесты периферии
  2. Тесты пройдены:
     just production
       ├── flash bootloader release
       └── flash app release

Установлено:     just · uv + spsdk · git
НЕ установлено:  docker · cmake · компилятор · ARM toolchain
```

Детальный `server.just` и пайплайн сервера — отдельная задача.

---

## Приложение А: минимальные версии

| Инструмент | Версия | Причина |
|---|---|---|
| `just` | 1.27.0 | поддержка `[group()]` |
| `uv` | 0.4.0 | стабильный lockfile формат |
| `docker` | 24.0.0 | Compose v2, `--build-arg` |
| `spsdk` | 3.7.x | совместимость с HAB yaml-форматом |
| ARM GCC | 13.3.rel1 | C11, LTO, текущий SDK |
| clang/clangd | 17 | поддержка `If:` в `.clangd` |

## Приложение Б: быстрая шпаргалка

```bash
# ── Первый запуск ──────────────────────────────────────────────
./scripts/bootstrap.sh           # инициализация хоста
# VSCode → Reopen in Container

# ── Внутри devcontainer (терминал VSCode) ──────────────────────
just --justfile just/build.just test-host
just --justfile just/build.just build-firmware-test-debug
just --justfile just/build.just build-app-release
just --justfile just/build.just hab-firmware-test-debug
just --justfile just/build.just hab-all-release
just --justfile just/build.just clean

# ── Хостовый терминал — прошивка ───────────────────────────────
just flash firmware_test debug   # во Flash
just flash firmware_test release
just flash bootloader release
just flash app release
just flash-ram firmware_test     # в RAM (быстро, без износа Flash)
just flash-production            # bootloader + app release

# ── Хостовый терминал — обслуживание ───────────────────────────
just scan                        # найти NXP USB-устройства
just sdp-status                  # проверить BootROM
just setup-tools                 # обновить spsdk после git pull
just check-deps                  # проверить версии инструментов
```

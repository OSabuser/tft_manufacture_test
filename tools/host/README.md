# tools/host — Окружение прошивки MIMXRT1052CVJ5B

Изолированное Python-окружение на базе [uv](https://docs.astral.sh/uv/) для сборки
HAB-образов и прошивки платы. Запускается на хост-машине разработчика —
**не внутри devcontainer**.

Поддерживаются два независимых способа прошивки:

| Способ | Скрипт | Интерфейс | Требование |
|---|---|---|---|
| USB SDP | `flash_usb.py` | USB ↔ ROM-загрузчик | BOOT_MODE = 01 |
| SWD | `flash_swd.py` | MCU-Link ↔ CMSIS-DAP | Плата в любом режиме |

---

## Структура

```bash
tools/host/
├── dcd/
│   ├── dcd.bin               ← DCD бинарник (инициализация SDRAM)
│   ├── ivt_flashloader.bin   ← NXP Flashloader (USB SDP)
│   ├── w25q64_fdcb.bin       ← FCB для W25Q64  (SWD flash)
│   ├── w25q128_fdcb.bin      ← FCB для W25Q128 (SWD flash) ← используется
│   └── w25q512_fdcb.bin      ← FCB для W25Q512 (SWD flash)
├── hab/
│   ├── hab_firmware_test_debug.yaml
│   ├── hab_firmware_test_release.yaml
│   ├── hab_bootloader_debug.yaml
│   ├── hab_bootloader_release.yaml
│   ├── hab_app_debug.yaml
│   └── hab_app_release.yaml
├── flash_usb.py              ← прошивка через USB ROM (SDP → blhost)
├── flash_swd.py              ← прошивка через SWD (pyocd, FCB+HAB)
├── HAB_GUIDE.md
├── pyproject.toml
├── uv.lock
└── README.md
```

---

## Предварительные требования

### 1. uv — один раз на машину

```bash
# macOS / Linux
curl -LsSf https://astral.sh/uv/install.sh | sh

# Windows
powershell -c "irm https://astral.sh/uv/install.ps1 | iex"
```

### 2. Зависимости проекта — один раз на репозиторий

```bash
cd tools/host
uv sync
```

### 3. udev правила — только Linux, один раз на машину

```bash
sudo tee /etc/udev/rules.d/99-nxp-mimxrt.rules << 'EOF'
# NXP BootROM — SDP режим
SUBSYSTEM=="usb", ATTR{idVendor}=="1fc9", ATTR{idProduct}=="0130", MODE="0666", GROUP="plugdev"
# NXP Flashloader
SUBSYSTEM=="usb", ATTR{idVendor}=="15a2", ATTR{idProduct}=="0073", MODE="0666", GROUP="plugdev"
EOF

sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -a -G plugdev $USER
# После usermod — перелогиниться!
```

---

## Карта Flash

```bash
0x60000000  ┌─────────────────────────────┐
            │  FCB — Flash Config Block   │  512 байт
            │  При USB SDP: пишет         │
            │  Flashloader автоматически. │
            │  При SWD: flash_swd.py      │
            │  берёт из dcd/*_fdcb.bin.   │
0x60001000  ├─────────────────────────────┤
            │  IVT — Image Vector Table   │  ← начало HAB-образа
            │  BDT — Boot Data Table      │
0x60001040  ├─────────────────────────────┤
            │  DCD — SDRAM init           │  ~1088 байт (fw_test, app)
0x60003000  ├─────────────────────────────┤
            │  Код прошивки               │
            │  (.text, .data, ...)        │
            └─────────────────────────────┘
```

---

## Способ 1 — USB SDP (`flash_usb.py`)

Прошивка через ROM-загрузчик. Требует перевода платы в режим Serial Downloader.

### Подготовка платы

```bash
1. BOOT_MOD_1 → 3V3
2. Reset
3. Подключить USB → плата определяется как VID:PID 1FC9:0130
```

После прошивки: `BOOT_MOD_1 → GND → Reset`.

### Запуск

```bash
# Предпочтительно через just (на хосте):
just host::flash firmware_test debug
just host::flash firmware_test release
just host::flash bootloader release
just host::flash app release

# Или напрямую:
cd tools/host
uv run python3 flash_usb.py --firmware firmware_test --build-type Debug
uv run python3 flash_usb.py --firmware app           --build-type Release

# Загрузка в RAM без записи во Flash (быстро, не изнашивает Flash):
uv run python3 flash_usb.py --firmware firmware_test --build-type Debug --ram-only
```

### Последовательность команд

```bash
Плата в SDP режиме (1FC9:0130)
    │
    ├─ sdphost write-file 0x20001C00 ivt_flashloader.bin
    └─ sdphost jump-address 0x20001C00
         │
         │  (ожидание до 10с пока Flashloader поднимется на 15A2:0073)
         │
    Flashloader (15A2:0073)
         ├─ configure-memory 0xC0000007   — инициализация FlexSPI NOR
         ├─ flash-erase-region 0x60000000
         ├─ configure-memory 0xF000000F   — запись FCB в 0x60000000
         ├─ write-memory 0x60001000 ← HAB-образ
         └─ reset
```

FCB генерируется Flashloader'ом автоматически из параметров FlexSPI — отдельный
`*_fdcb.bin` не нужен.

---

## Способ 2 — SWD (`flash_swd.py`)

Прошивка через отладочный пробник (MCU-Link, CMSIS-DAP). Плата остаётся в
нормальном режиме загрузки — переключать `BOOT_MOD_1` не нужно.

**После записи обязателен power cycle** — VECTRESET не реинициализирует FlexSPI,
Boot ROM не стартует без холодного старта.

### Почему нужен FCB при SWD

При USB SDP FlexSPI конфигурируется ROM-загрузчиком через DCD. При SWD
flash-алгоритм pyOCD пишет данные напрямую — Boot ROM при cold-start читает FCB
первым и по нему конфигурирует FlexSPI. Без FCB плата не стартует.

`flash_swd.py` собирает итоговый образ перед записью:

```
0x60000000  *_fdcb.bin  (512 байт)  — FCB
0x60000200  0xFF × 3584 байт        — padding (erased flash value)
0x60001000  *_hab.bin               — HAB-образ (ivtOffset = 0x1000)
```

Всё умещается в один 64KB-сектор — стирается и записывается за одну транзакцию.

### FCB-файлы

| Файл | Микросхема | Режим |
|---|---|---|
| `w25q64_fdcb.bin`  | Winbond W25Q64  | Quad SPI |
| `w25q128_fdcb.bin` | Winbond W25Q128 | Quad SPI |
| `w25q512_fdcb.bin` | Winbond W25Q512 | Quad SPI |

Активный FCB задаётся в `.env`: `FCB_PATH=tools/host/dcd/w25q128_fdcb.bin`.

FCB-файлы получены из NXP SecureProvisioningTool и хранятся в репозитории —
пересоздавать не нужно.

### Запуск

```bash
# Предпочтительно через just (на хосте):
just host::flash-swd-test-debug
just host::flash-swd-test-release
just host::flash-swd-bootloader-debug
just host::flash-swd-bootloader-release
just host::flash-swd-app-debug
just host::flash-swd-app-release

# Или напрямую:
cd tools/host
uv run --directory ../hil python3 flash_swd.py --firmware firmware_test --build-type Debug
uv run --directory ../hil python3 flash_swd.py --firmware app           --build-type Release

# Собрать образ без записи (для проверки):
uv run --directory ../hil python3 flash_swd.py --firmware firmware_test --build-type Debug --dry-run
```

### Конфигурация flash_swd.py

Приоритет: аргументы CLI > переменные окружения > defaults.

| Переменная | CLI-аргумент | Default |
|---|---|---|
| `PYOCD_TARGET` | `--target` | `mimxrt1050_quadspi` |
| `PYOCD_FREQUENCY` | `--frequency` | `4000000` |
| `BUILD_DIR` | — | `<repo>/build` |
| `FCB_PATH` | `--fcb` | `tools/host/dcd/w25q128_fdcb.bin` |

Переменные задаются в `.env` и экспортируются через `just` (`set export`).

---

## Сравнение способов

| | USB SDP | SWD |
|---|---|---|
| Переключение BOOT_MODE | Нужно | Не нужно |
| Power cycle после записи | Не нужен | **Обязателен** |
| FCB в образе | Не нужен (Flashloader пишет сам) | **Нужен** (`*_fdcb.bin`) |
| Скорость записи | ~50–100 kB/s | ~8–10 kB/s |
| Совместимость с отладкой | Раздельно | MCU-Link монопольный |
| Производственный сценарий | ✓ | — |
| Итеративная разработка | Неудобно (смена режима) | ✓ |

---

## Пайплайн: сборка HAB-образов

Выполняется **внутри devcontainer**:

```bash
just build::hab-firmware-test-debug    # → build/Debug/firmware_test_hab.bin
just build::hab-firmware-test-release  # → build/Release/firmware_test_hab.bin
just build::hab-bootloader-debug
just build::hab-bootloader-release
just build::hab-app-debug
just build::hab-app-release
just build::hab-all-release            # все три Release за один раз
```

---

## DCD — инициализация SDRAM

`dcd/dcd.bin` — бинарный блоб команд, который BootROM выполняет до передачи
управления прошивке. Инициализирует PLL, CCM clock gates, SEMC контроллер
и микросхему SDRAM (MT48LCxxM4).

Файл хранится в репозитории в бинарном виде и **не требует пересборки**.

```bash
Цель: MT48LC16M16A2P-6A, 32 MB, шина 16 бит, CS0
  SEMC BR0: base=0x80000000, size=32MB, VLD=1
  SEMC BR1–BR3: VLD=0
```

| Прошивка | DCD | Причина |
|---|---|---|
| `firmware_test` | ✓ | тесты работают с SDRAM |
| `app` | ✓ | FreeRTOS heap и буферы LCDIF в SDRAM |
| `bootloader` | ✗ | загрузчик не использует SDRAM |

---

## Flashloader

`dcd/ivt_flashloader.bin` — NXP-программа, загружаемая в RAM через SDP.

```bash
Entry point:    0x20002401
Загрузка по:    0x20001C00
VID:PID после:  15A2:0073
Источник:       MCUXpresso Secure Provisioning Tool 25.12
```

---

## Диагностика

```bash
# Найти подключённые NXP устройства
uv run nxpdevscan

# Проверить связь с BootROM (плата в SDP-режиме)
sdphost -u 0x1FC9,0x0130 -- error-status

# Проверить что Flashloader отвечает
blhost -u 0x15A2,0x0073 -- get-property 1 0

# Проверить что pyOCD видит таргет (для SWD)
just host::debug-list-targets
```

### Типичные ошибки

| Симптом | Причина | Решение |
|---|---|---|
| `USB HID device not found: 1FC9:0130` | Плата не в SDP режиме | Проверить `BOOT_MOD_1` → 3V3 и Reset |
| Flashloader timeout после jump | `ivt_flashloader.bin` повреждён | Взять из SPT 25.12 |
| Плата не стартует после USB SDP | `BOOT_MOD_1` не переключён обратно | `BOOT_MOD_1` → GND, Reset |
| Плата не стартует после SWD flash | Power cycle не был выполнен | Отключить и подключить питание |
| Плата не стартует после SWD flash | Неверный FCB (другая Flash-микросхема) | Проверить `FCB_PATH` в `.env` |
| `skipped N bytes` при SWD flash | pyOCD считает содержимое актуальным | Добавить `--erase chip` или `--erase sector` |

---

## Обновление зависимостей

```bash
cd tools/host
uv add "spsdk==X.Y.Z"
uv sync
git add uv.lock pyproject.toml
```

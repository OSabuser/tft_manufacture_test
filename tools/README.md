# tools/host — Окружение прошивки MIMXRT1052CVJ5B

Изолированное Python-окружение на базе [uv](https://docs.astral.sh/uv/) для сборки
HAB-образов и прошивки платы через USB. Запускается на хост-машине разработчика —
**не внутри devcontainer**.

---

## Структура

```bash
tools/host/
├── dcd/
│   ├── dcd.bin                   ← DCD бинарник (инициализация SDRAM, коммитить как есть)
│   └── ivt_flashloader.bin       ← NXP Flashloader (коммитить)
├── hab/
│   ├── hab_firmware_test.yaml    ← HAB: Debug + DCD (входной контроль)
│   ├── hab_bootloader.yaml       ← HAB: Release, без DCD
│   └── hab_app.yaml              ← HAB: Release + DCD (боевая прошивка)
├── flash_usb.py                  ← скрипт прошивки через USB
├── pyproject.toml                ← зависимости (spsdk==3.7.x)
├── uv.lock                       ← lockfile (коммитить)
└── README.md
```

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

Без этого для работы с USB нужен `sudo`:

```bash
sudo tee /etc/udev/rules.d/99-nxp-mimxrt.rules << 'EOF'
# NXP BootROM — SDP режим (BOOT_MOD_1 = 3V3)
SUBSYSTEM=="usb", ATTR{idVendor}=="1fc9", ATTR{idProduct}=="0130", MODE="0666", GROUP="plugdev"
# NXP Flashloader — после jump-address
SUBSYSTEM=="usb", ATTR{idVendor}=="15a2", ATTR{idProduct}=="0073", MODE="0666", GROUP="plugdev"
EOF

sudo udevadm control --reload-rules && sudo udevadm trigger
sudo usermod -a -G plugdev $USER
# После usermod — перелогиниться!
```

---

## Карта Flash (W25Q64FVSSIG, 8 MB)

```bash
0x60000000  ┌─────────────────────────────┐
            │  FCB — Flash Config Block   │  512 байт
            │  (пишет Flashloader,        │
            │   не входит в HAB образ)    │
0x60001000  ├─────────────────────────────┤
            │  IVT — Image Vector Table   │  ← начало HAB образа
            │  BDT — Boot Data Table      │
0x60001040  ├─────────────────────────────┤
            │  DCD — SDRAM init           │  ~1088 байт (для fw_test и app)
0x60003000  ├─────────────────────────────┤
            │  Код прошивки               │
            │  (.text, .data, ...)        │
            └─────────────────────────────┘
```

---

## Пайплайн: сборка HAB образа

```bash
cd tools/host/hab

# firmware_test (Debug) — входной контроль платы
uv run nxpimage hab export --force \
    -c hab_firmware_test.yaml \
    -o ../../../../build/Debug/firmware_test_hab.bin

# bootloader (Release)
uv run nxpimage hab export --force \
    -c hab_bootloader.yaml \
    -o ../../../../build/Release/bootloader_hab.bin

# app (Release) — боевая прошивка
uv run nxpimage hab export --force \
    -c hab_app.yaml \
    -o ../../../../build/Release/app_hab.bin
```

### Проверка образа после сборки

```bash
uv run nxpimage hab parse -b ../../../../build/Debug/firmware_test_hab.bin
```

Ожидаемый результат: IVT с корректным `entry`, DCD с тегом `0xD2`,
`csf = 0x00000000` (unsigned).

---

## Пайплайн: прошивка через USB

### Подготовка платы

Перевести плату в режим SDP (Serial Download Protocol):

1. Подтянуть `BOOT_MOD_1` к `3V3`
2. Reset
3. Подключить USB к ПК

В этом режиме плата определяется как `VID:PID = 1FC9:0130`.

### Запуск скрипта

```bash
cd tools/host

# firmware_test
uv run python3 flash_usb.py --firmware firmware_test --build-type Debug

# app
uv run python3 flash_usb.py --firmware app --build-type Release

# bootloader
uv run python3 flash_usb.py --firmware bootloader --build-type Release
```

После прошивки: вернуть `BOOT_MOD_1` к `GND`, Reset — плата стартует из Flash.

### Последовательность команд внутри скрипта

```bash
Плата в SDP режиме (1FC9:0130)
    │
    ├─ sdphost write-file 0x20001C00 ivt_flashloader.bin
    └─ sdphost jump-address 0x20001C00
         │
         │  (ожидание до 10с пока Flashloader поднимется на 15A2:0073)
         │
    Flashloader (15A2:0073)
         │
         │  Шаг 1 — инициализация FlexSPI NOR контроллера
         ├─ blhost fill-memory 0x2000 4 0xC0000007 word
         ├─ blhost configure-memory 9 0x2000
         │
         │  Шаг 2 — стирание Flash
         ├─ blhost flash-erase-region 0x60000000 <size> 0    (memoryId=0, XIP)
         │
         │  Шаг 3 — запись FCB в 0x60000000
         │  (ПОСЛЕ стирания! Flashloader генерирует FCB из параметров шага 1)
         ├─ blhost fill-memory 0x2000 4 0xF000000F word
         ├─ blhost configure-memory 9 0x2000
         │
         │  Шаг 4 — запись HAB образа начиная с 0x60001000
         ├─ blhost write-memory 0x60001000 *_hab.bin 0       (memoryId=0, XIP)
         └─ blhost reset
```

> **Почему FCB не в образе?**
> Flashloader генерирует FCB автоматически из параметров FlexSPI (option word
> `0xC0000007`). Запись FCB через `0xF000000F` — это отдельная команда,
> выполняемая строго после `flash-erase-region`, иначе FCB будет затёрт следующим
> стиранием.

---

## DCD — инициализация SDRAM

`dcd/dcd.bin` — бинарный блоб команд, который BootROM выполняет до передачи
управления прошивке. Инициализирует PLL, CCM clock gates, SEMC контроллер
и микросхему SDRAM (MT48LCxxM4).

Файл хранится в репозитории в бинарном виде и **не требует пересборки** — он
меняется только при изменении схемотехники.

```bash
Цель: MT48LC16M16A2P-6A, 32 MB, шина 16 бит, CS0
  SEMC BR0: base=0x80000000, size=32MB, VLD=1
  SEMC BR1–BR3: VLD=0  (один чип, один CS)
```

Использование DCD по прошивкам:

| Прошивка | DCD | Причина |
|---|---|---|
| `firmware_test` | ✓ | тесты работают с SDRAM |
| `app` | ✓ | FreeRTOS heap и буферы LCDIF размещены в SDRAM |
| `bootloader` | ✗ | загрузчик не использует SDRAM, инициализация в app |

---

## Flashloader

`dcd/ivt_flashloader.bin` — NXP-программа, загружаемая в RAM через SDP.
Предоставляет `blhost` доступ к Flash, которого нет через BootROM напрямую.

```bash
Entry point:    0x20002401
Загрузка по:    0x20001C00
VID:PID после старта: 15A2:0073
Источник:       MCUXpresso Secure Provisioning Tool 25.12
```

---

## Диагностика

```bash
# Найти подключённые NXP устройства
uv run nxpdevscan

# Проверить связь с BootROM через SDP
sdphost -u 0x1FC9,0x0130 -- error-status

# Проверить что Flashloader отвечает (после jump-address)
blhost -u 0x15A2,0x0073 -- get-property 1 0

# Версии инструментов
uv run nxpimage --version
uv run blhost --version
```

### Типичные ошибки

| Симптом | Причина | Решение |
|---|---|---|
| `USB HID device not found: 1FC9:0130` | Плата не в SDP режиме | Проверить `BOOT_MOD_1` → 3V3 и Reset |
| Flashloader timeout после jump | `ivt_flashloader.bin` повреждён или не тот | Взять из SPT 25.12 |
| Плата не стартует после прошивки | `BOOT_MOD_1` не переключён обратно | Вернуть `BOOT_MOD_1` → GND, Reset |
| Плата зависает сразу после старта | DCD завис (неверный `dcd.bin`) | Использовать только верифицированный `dcd.bin` |
| Плата стартует, периферия не работает | CCM clock gates в DCD отключают нужные клоки | Проверить `dcd.bin` — только верифицированный вариант |

---

## Обновление зависимостей

```bash
cd tools/host
uv add "spsdk==X.Y.Z"
uv sync
git add uv.lock pyproject.toml
```

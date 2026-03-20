# Контекст проекта — состояние на текущий момент

## Проект

Монорепозиторий `tft_manufacture_test` для **MIMXRT1052CVJ5B** (Cortex-M7, NXP i.MX RT1052). Три firmware-проекта: `firmware/test` (входной контроль, bare-metal), `firmware/bootloader` (A/B обновление, bare-metal), `firmware/tft_app` (боевая прошивка, FreeRTOS). Сборка в devcontainer, операции с железом на хосте.

Стек: CMake + Ninja + CMakePresets, `just` как task runner (модули `build.just`, `host.just`, `ci.just`), `uv` для Python-зависимостей, devcontainer (VSCode + arm-none-eabi-gcc + clangd).

---

## Что сделано в этой сессии

### 1. Отладка через SWD — настроена и работает

**Архитектура:**
```
Хост: pyocd gdbserver :3333 ← MCU-Link (CMSIS-DAP) ← SWD ← MIMXRT1052
DevContainer: arm-none-eabi-gdb → host.docker.internal:3333
VSCode: cortex-debug (servertype: external)
```

**Файлы:**
- `pyocd_debug.yaml` — конфиг pyOCD (`target_override: mimxrt1050_quadspi`, `frequency: 4000000`, `rtt.enabled: true`, `rtt.port: 4445`)
- `.vscode/launch.json` — три конфигурации cortex-debug
- `.vscode/tasks.json` — `preLaunchTask` для каждой конфигурации + `rtt:connect`
- `just/host.just` — рецепты `debug-server`, `flash-swd-*`

**Параметры из `.env`:**
```
GDB_PORT=3333
PYOCD_TARGET=mimxrt1050_quadspi
PYOCD_FREQUENCY=4000000
FCB_PATH=tools/host/dcd/w25q128_fdcb.bin
```

**Конфигурации launch.json:**
- `🐛 Debug: firmware_test` — `build/Debug/firmware_test.elf`
- `🐛 Debug: bootloader` — `build/Debug/bootloader.elf`
- `🐛 Debug: tft_app (FreeRTOS)` — `build/Debug/app.elf`, `"rtos": "FreeRTOS"`

Все три: `loadFiles: []` (не перепрошивают), `runToEntryPoint: main`, SVD из `bsp/generated/startup/MIMXRT1052.xml`.



---

### 2. Прошивка через SWD — настроена и работает

**Проблема:** GDB `load` не работает для XIP-прошивок на IMXRT — Boot ROM при cold-start читает FCB по `0x60000000`, а ELF-секции кладутся без FCB.

**Решение:** `tools/host/flash_swd.py` собирает итоговый образ:
```
0x60000000  w25q128_fdcb.bin (512 байт) — FCB для W25Q128 Quad SPI
0x60000200  0xFF × 3584 байт            — padding
0x60001000  *_hab.bin                   — IVT + DCD + код
```

Всё в одном 64KB-секторе — стирается и записывается за одну транзакцию.


**Рецепты в host.just:**
```
just host::flash-swd-test-debug
just host::flash-swd-test-release
just host::flash-swd-bootloader-debug
just host::flash-swd-bootloader-release
just host::flash-swd-app-debug
just host::flash-swd-app-release
```

Скрипт читает конфигурацию из окружения (`PYOCD_TARGET`, `PYOCD_FREQUENCY`, `BUILD_DIR`, `FCB_PATH`). Относительные пути из `.env` автоматически разрешаются от `REPO_ROOT`.

**Рабочий цикл А (прошивка уже в Flash):**
```
just host::debug-server  →  VSCode: 🐛 Debug → F5
```

**Рабочий цикл Б (прошить через SWD + отладить):**
```
just build::hab-firmware-test-debug   (в контейнере)
just host::flash-swd-test-debug       (на хосте)
⚡ power cycle
just host::debug-server
VSCode: 🐛 Debug → F5
```

---

### 3. RTT — решено отказаться, заменить на UART

**Почему отказались от RTT:**
- `servertype: external` в cortex-debug официально не поддерживает RTT нативно
- Workaround через `postLaunchCommands` + `rtt_client.py` работает, но требует ручного запуска RTT-клиента после F5
- Автозапуск через `preLaunchTask` зависает (клиент стартует до того как pyOCD открыл порт)
- Итоговое UX: два ручных действия вместо одного

**Вывод:** RTT остаётся в проекте как возможность (код в `lib/SEGGER`, `SEGGER_RTT_ENABLED=ON` в Debug-пресете), но для логов использовать не будем.

---

### 4. UART как канал логов — решение принято

**BSP:** `bsp/uart_host` — `bsp_uart_host_write_str()` / `bsp_uart_host_write()`.

TX — `LPUART_WriteBlocking` (blocking polling). Для логов это нормально:
- Bare-metal: нет проблем
- FreeRTOS: задача вытесняется по таймеру, но мьютекс нужен если несколько задач пишут лог

**Ограничения bsp_uart_host для логгера:**
- Нельзя вызывать из ISR
- В FreeRTOS-контексте нужен мьютекс на уровне логгера (не в bsp_uart_host)

**Мониторинг логов на хосте:**
```
just host::uart-monitor
```
Использует `python3 -m serial.tools.miniterm` (pyserial, кроссплатформенно — macOS/Linux/Windows). Порт и бод из `.env` (`HIL_VCOM_PORT`, `HIL_VCOM_BAUD`).

---

## Следующий шаг — слой логгирования

Планируется добавить логгер поверх `bsp_uart_host`. Кандидат — **log.c** (rxi, однофайловый, MIT). Нужно:

1. Вендоринг `log.h` / `log.c` в `lib/log/` или `utils/log/`
2. CMake-таргет
3. Callback-адаптер → `bsp_uart_host_write_str()`
4. Мьютекс для FreeRTOS (в callback, не в bsp)
5. Запрет вызова из ISR (задокументировать)
6. Красивый вывод: уровень, файл, строка, ANSI-цвета

---

## Структура файлов отладки (итог)

```
.
├── .env                                     # GDB_PORT, PYOCD_*, RTT_PORT, FCB_PATH
├── .vscode/
│   ├── launch.json                          # 3 конфигурации cortex-debug
│   └── tasks.json                           # build:*, rtt:connect, uart-monitor
├── pyocd_debug.yaml                         # target, frequency, rtt config
├── bsp/generated/startup/MIMXRT1052.xml     # SVD
├── just/host.just                           # debug-server, flash-swd-*, uart-monitor
└── tools/
    ├── hil/                                 # uv-проект: pyocd, pyserial, pytest
    └── host/
        ├── flash_swd.py                     # FCB+HAB → pyocd flash
        ├── rtt_client.py                    # RTT TCP-клиент (retry-loop)
        └── dcd/
            └── w25q128_fdcb.bin             # FCB для W25Q128 Quad SPI
```
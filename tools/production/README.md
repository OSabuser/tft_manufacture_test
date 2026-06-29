# service-tui — TUI сервисного инженера

TUI-приложение для диагностики и прошивки платы **MIMXRT1052CVJ5B** на сервисе.  
Написано на Python + [Textual](https://textual.textualize.io/). Работает на Linux, macOS, Windows.

---

## Концепция

```mermaid
graph LR
    subgraph PC["Сервисный ПК"]
        TUI["service-tui\n(Textual App)"]
        subgraph tools["tools/"]
            FC["firmware_client.py\nUSB CDC ACM"]
            M5["m5_client.py\nSerial JSON-lines"]
            FL["flasher.py\n→ tools/host/flash_usb.py"]
        end
        TUI --> FC
        TUI --> M5
        TUI --> FL
    end

    subgraph Board["Плата TFT (MIMXRT1052)"]
        FW["firmware_test\n(USB CDC)"]
        ROM["BootROM\n(SDP 1FC9:0130)"]
    end

    subgraph HIL["HIL стенд (опционально)"]
        M5HW["M5StampPLC\nRLY1–4 + CAN"]
    end

    FC <-->|"JSON-lines\nVID:PID 1996:00AD"| FW
    FL -->|"sdphost + blhost\nVID:PID 1FC9:0130"| ROM
    M5 <-->|"JSON-lines\nSerial"| M5HW
    M5HW -->|"RLY1–4"| Board
```

---

## Два режима работы

Режим определяется автодетектом USB при старте и меняется динамически.

```mermaid
stateDiagram-v2
    [*] --> WAITING : запуск TUI

    WAITING --> FLASHING   : обнаружен VID:PID 1FC9:0130\n(BootROM SDP)
    WAITING --> DIAGNOSING : обнаружен VID:PID 1996:00AD\n+ session_start по CDC

    FLASHING --> WAITING   : прошивка завершена / плата отключена
    FLASHING --> DIAGNOSING: плата перезагружена в нормальный режим

    DIAGNOSING --> WAITING : плата отключена
    DIAGNOSING --> FLASHING: плата переведена в SDP (перемычка)
```

### Режим A — Прошивка

Триггер: обнаружен BootROM SDP (`1FC9:0130`).

```bash
┌─ Прошивка платы ─────────────────────────────────┐
│  Обнаружен BootROM (SDP режим)                     │
│                                                    │
│  Что прошить?                                      │
│  ◉ firmware_test  (диагностика)                    │
│  ○ Production     (bootloader + tft_app)           │
│                                                    │
│  ████████████░░░░░░  64%   Запись во Flash...      │
└────────────────────────────────────────────────────┘
```

### Режим B — Диагностика

Триггер: CDC-порт `1996:00AD` обнаружен и `ping→pong` прошёл.

```
┌─ Диагностика  fw:0.1.4  UID:A1B2C3D4E5F60011 ─────┐
│  M5StampPLC: ✓            │  Результаты:            │
├───────────────────────────┤  sdram    ✓ PASS        │
│  ☑ SDRAM 32 MB            │  qspi     ✓ PASS        │
│  ☑ QSPI Flash             │  usd      ✗ FAIL        │
│  ☑ microSD                │    mount failed: 5      │
│  ☑ TFT Display            │  display  ✓ PASS        │
│  ☑ Кнопки                 │  buttons  ✓ PASS        │
│  ☑ MQS Audio              │  mqs      ✓ PASS        │
│  ☑ CAN loopback  [HIL]    │  can      … running     │
│  ☑ Оптовходы     [HIL]    │  opto     ○ pending     │
├───────────────────────────┴─────────────────────────┤
│  [ Запустить выбранные ]      [ Все тесты ]         │
│  ████████████████░░░░  80%    Тест: can             │
├─────────────────────────────────────────────────────┤
│  ⚠ Экран залит красным цветом?                      │
│  [ ✓ Да ]   [ ✗ Нет ]                              │
└─────────────────────────────────────────────────────┘
```

---

## Архитектура приложения

```mermaid
graph TB
    subgraph TUI["tui.py — Textual App"]
        WS["WaitingScreen"]
        FS["FlashScreen"]
        DS["DiagScreen"]
    end

    subgraph Core["app/"]
        OR["orchestrator.py\nмаршрутизация confirm_request"]
        FW["firmware_client.py\nasync CDC клиент"]
        M5["m5_client.py\nasync M5 клиент"]
        FL["flasher.py\nsubprocess flash_usb.py"]
        MD["models.py\nTestInfo · TestResult\nSessionState · ConfirmRequest"]
    end

    DS --> OR
    OR --> FW
    OR --> M5
    FS --> FL
    DS --> MD
    OR --> MD
```

---

## Обработка confirm_request

Маршрутизация определяется по `id` поля `confirm_request`:

```mermaid
flowchart TD
    CR["confirm_request\nот firmware_test"]

    CR --> R{confirm_request.id}

    R -->|"opto_*"| HIL_OPTO["HIL: M5 relay_set\n→ settle → confirm"]
    R -->|"can_rx_ready"| HIL_CAN_RX["HIL: M5 can_send\n→ confirm"]
    R -->|"can_tx_verify"| HIL_CAN_TX["HIL: M5 can_recv\n→ verify → confirm"]
    R -->|"btn*"| BTN["показать инструкцию\nне отправлять confirm\nждать test_result"]
    R -->|"всё остальное"| OP["показать оператору\nprompt + OK/FAIL\n+ countdown"]

    HIL_OPTO --> AUTO["CONFIRM_RESOLVED\n(автоматически)"]
    HIL_CAN_RX --> AUTO
    HIL_CAN_TX --> AUTO
    OP --> WAIT["CONFIRM_NEEDED\nждём resolve_operator_confirm()"]
    WAIT --> SEND["send_confirm(id, confirmed)"]
    AUTO --> SEND
```

| confirm id      | Кто отвечает | Действие TUI                           |
| --------------- | ------------ | -------------------------------------- |
| `opto_*`        | M5 авто      | показать прогресс                      |
| `can_rx_ready`  | M5 авто      | показать прогресс                      |
| `can_tx_verify` | M5 авто      | показать прогресс                      |
| `btn*`          | физика       | показать инструкцию, ждать test_result |
| всё остальное   | оператор     | prompt + OK/FAIL + countdown           |

---

## Структура

```bash
tools/production/
├── pyproject.toml          ← зависимости: textual, pyserial, python-dotenv, pyinstaller
├── uv.lock
├── main.py                 ← точка входа: asyncio + Textual App
├── app/
│   ├── tui.py              ← Textual App, экраны (WaitingScreen, FlashScreen, DiagScreen)
│   ├── firmware_client.py  ← async USB CDC клиент firmware_test
│   ├── m5_client.py        ← async M5StampPLC клиент
│   ├── flasher.py          ← subprocess → tools/host/flash_usb.py
│   ├── orchestrator.py     ← confirm_request маршрутизатор
│   └── models.py           ← AppMode, TestInfo, TestResult, SessionState, …
└── README.md               ← этот файл
```

---

## Конфигурация (`.env`)

Файл `.env` в корне репозитория — единый источник конфигурации.

```ini
# USB VID:PID — BootROM SDP (менять нельзя, NXP ROM)
BOOTROM_VID=1fc9
BOOTROM_PID=0130

# USB VID:PID — firmware_test CDC (наше устройство)
SERVICE_CDC_VID=1996
SERVICE_CDC_PID=00ad

# Пути к бинарям (опционально, TUI ищет в build/ автоматически)
FIRMWARE_TEST_BIN=build/Release/firmware_test_hab.bin
PRODUCTION_BIN_BOOT=build/Release/bootloader_hab.bin
PRODUCTION_BIN_APP=build/Release/tft_app_hab.bin
```

---

## Запуск

### Из монорепозитория (разработчик)

```bash
# Установить зависимости
just host::service-setup

# Запустить TUI
just host::service-tui
```

### Standalone-бинарь (сервисник)

Скачать `service_tui` из [GitHub Releases](https://github.com/OSabuser/tft_manufacture_test/releases) и запустить двойным кликом.  
Для сборки из исходников:

```bash
just host::service-build
# → tools/production/dist/service_tui
```

---

## Рабочий процесс сервисника

```mermaid
sequenceDiagram
    participant OP as Оператор
    participant TUI as service-tui
    participant FW as firmware_test (CDC)
    participant M5 as M5StampPLC

    OP->>TUI: запустить service_tui
    OP->>TUI: подключить плату USB (нормальный режим)
    TUI->>FW: ping → pong (CDC автодетект)
    TUI->>FW: list_tests
    FW-->>TUI: TestInfo × 8
    TUI-->>OP: показать список тестов

    OP->>TUI: выбрать тесты → Запустить
    TUI->>FW: run_selected([...])

    loop Для каждого теста
        FW-->>TUI: test_begin
        TUI-->>OP: прогресс

        alt HIL тест (opto/can)
            FW-->>TUI: confirm_request
            TUI->>M5: relay_set / can_send
            TUI->>FW: confirm(true)
        else Интерактивный (display/mqs)
            FW-->>TUI: confirm_request
            TUI-->>OP: показать prompt + countdown
            OP->>TUI: OK / FAIL
            TUI->>FW: confirm(true/false)
        else Кнопки
            FW-->>TUI: confirm_request (инструкция)
            TUI-->>OP: "Нажмите кнопку..."
            OP->>FW: физическое нажатие
        end

        FW-->>TUI: test_result
        TUI-->>OP: результат теста
    end

    FW-->>TUI: summary
    TUI-->>OP: итог: PASS / FAIL
```

---

## Зависимости

| Пакет           | Версия | Назначение               |
| --------------- | ------ | ------------------------ |
| `textual`       | ≥ 0.80 | TUI фреймворк            |
| `pyserial`      | ≥ 3.5  | USB CDC ACM + M5 Serial  |
| `python-dotenv` | ≥ 1.0  | загрузка `.env`          |
| `pyinstaller`   | ≥ 6.0  | сборка standalone-бинаря |

**Runtime зависимость (не в pyproject.toml):**  
`tools/host/flash_usb.py` вызывается через `subprocess` с `uv run` — `tools/host/` uv-проект должен быть инициализирован (`just host::setup-tools`).

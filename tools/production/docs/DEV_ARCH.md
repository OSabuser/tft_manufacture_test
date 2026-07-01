# service-tui — техническая архитектура

> Компонент: `tools/production/` — TUI сервисного инженера (диагностика и
> прошивка платы MIMXRT1052CVJ5B).
> Документ описывает внутреннее устройство: структуру модулей, протокол
> взаимодействия с firmware/M5, экранную архитектуру Textual, известные
> особенности фреймворка.
> Пользовательская документация (экраны, запуск, конфигурация,
> рабочие процессы сервисника) — в [README.md](README.md).

---

## 1. Структура проекта

```bash
tools/production/
├── main.py                              ← точка входа (10 строк)
├── pyproject.toml                       ← зависимости uv
├── uv.lock
└── app/
    ├── app.py                           ← ServiceApp — роутинг экранов, жизненный цикл клиентов
    ├── app.tcss                         ← единый файл стилей для всех экранов
    ├── models.py                        ← все типы данных (dataclass/Enum)
    ├── boot_art.py                      ← LOGO_ART — растеризованный логотип для WaitingScreen
    ├── firmware_client.py               ← async USB CDC клиент firmware_test (UTF-8)
    ├── m5_client.py                     ← async M5StampPLC клиент (Serial JSON-lines, UTF-8)
    ├── flasher.py                       ← subprocess-обёртка над tools/host/flash_usb.py
    ├── orchestrator.py                  ← маршрутизация confirm_request, progress, таймауты
    ├── widgets/
    │   ├── __init__.py
    │   └── app_frame.py                 ← AppFrame — общий адаптивный контейнер всех экранов
    └── screens/
        ├── __init__.py                  ← реэкспорт: WaitingScreen, FlashScreen, PostFlashScreen, DiagScreen
        ├── waiting.py                   ← WaitingScreen — ожидание USB, лого, версия, баннер причины возврата
        ├── flash.py                     ← FlashScreen — прошивка / chip erase
        ├── post_flash.py                ← PostFlashScreen — промпт смены BootMode после прошивки
        ├── connection_watcher.py        ← ConnectionWatcherMixin — мониторинг обрыва USB
        └── diag/
            ├── __init__.py              ← DiagScreen — координатор диагностики
            ├── test_list.py             ← TestListPanel — чекбоксы тестов, Выбрать/Снять все
            ├── results.py               ← ResultsPanel — DataTable результатов
            └── confirm_panel.py         ← ConfirmPanel — prompt оператора + countdown
```

---

## 2. Концепция

```mermaid
graph LR
    subgraph PC["Сервисный ПК"]
        TUI["service-tui\n(Textual App)"]
        subgraph app["app/"]
            FC["firmware_client.py\nUSB CDC ACM, UTF-8"]
            M5["m5_client.py\nSerial JSON-lines, UTF-8"]
            FL["flasher.py\nsubprocess + pyusb detect"]
            OR["orchestrator.py\nconfirm/progress/timeout router"]
        end
        TUI --> FC & M5 & FL & OR
    end

    subgraph Board["Плата TFT (MIMXRT1052)"]
        FW["firmware_test\n(USB CDC)"]
        ROM["BootROM SDP\n(1FC9:0130)"]
    end

    subgraph HIL["HIL стенд (опционально)"]
        M5HW["M5StampPLC\nRLY1–4 + CAN"]
    end

    subgraph Host["tools/host/"]
        FU["flash_usb.py\nsdphost + blhost"]
    end

    FC   <-->|"JSON-lines UTF-8\nVID:PID 1996:00AD"| FW
    FL    -->|"subprocess uv run"| FU
    FU    -->|"sdphost + blhost\nVID:PID 1FC9:0130"| ROM
    M5   <-->|"JSON-lines\nSerial"| M5HW
    M5HW  -->|"RLY1–4"| Board
```

> **Детект USB:** `Flasher.detect_sdp()`/`detect_cdc()` используют `pyusb` как
> основной метод (BootROM SDP не создаёт serial-порт на macOS и невидим через
> `pyserial.list_ports`), с fallback на `serial.tools.list_ports` для CDC.
> M5StampPLC детектируется отдельно в `m5_client.py` тем же способом
> (`pyusb`, VID/PID из `.env` — см. раздел 5).

---

## 3. Диаграмма состояний приложения

Состояние определяется автодетектом USB и меняется динамически без
перезапуска TUI. При потере соединения сессия разрывается полностью — TUI не
пытается восстановить прежнее состояние, а стартует заново с `WaitingScreen`.

```mermaid
stateDiagram-v2
    [*] --> WAITING : запуск TUI

    WAITING --> FLASHING   : VID:PID 1FC9:0130\n(BootROM SDP)
    WAITING --> DIAGNOSING : VID:PID 1996:00AD\n+ ping→pong по CDC

    FLASHING --> POST_FLASH : firmware_test прошит успешно
    FLASHING --> WAITING    : Production/Custom прошит, ошибка,\nили потеря USB в простое

    POST_FLASH --> WAITING : оператор подтвердил / таймаут 40с

    DIAGNOSING --> WAITING : DiagDone / ESC /\nпотеря USB в простое
    DIAGNOSING --> FLASHING: плата переведена в SDP (перемычка BOOT_MOD)
```

Состояния соответствуют `AppMode` в `models.py`; переключение экранов —
`ServiceApp.push_screen()`/`switch_screen()` в `app.py`, реагирующий на
сообщения `DeviceDetected`/`FlashDone`/`DiagDone`.

---

## 4. Обработка confirm_request

Маршрутизация реализована в `Orchestrator._handle_confirm()` по значению
`confirm_request.id`. Помимо confirm, протокол v2 определяет `progress` —
внутришаговые информационные события долгих тестов (сейчас только `usd`), не
требующие ответа.

```mermaid
flowchart TD
    EV["Событие от firmware_test"]
    EV --> T{type}

    T -->|"confirm_request"| R{confirm_request.id}
    T -->|"progress"| PROG["TEST_PROGRESS\nотобразить фазу в прогресс-строке"]
    T -->|"test_begin / test_result / summary"| STD["стандартная обработка"]
    T -->|"_timeout (синтетическое,\nот FirmwareClient)"| TO["синтезировать FAIL\nдля зависшего теста\n+ гарантированный SUMMARY"]
    T -->|"неизвестный тип"| LOG["logger.debug — НЕ ошибка,\nне показывается оператору"]

    R -->|"opto_*"| HIL_OPTO["M5: relay_set → settle → send_confirm"]
    R -->|"can_rx_ready"| HIL_CAN_RX["M5: can_send → send_confirm"]
    R -->|"can_tx_verify"| HIL_CAN_TX["M5: can_recv → verify → send_confirm"]
    R -->|"btn*"| BTN["show_buttons_hint, БЕЗ JSON-confirm"]
    R -->|"остальное"| OP["show_operator + countdown\nждать resolve_operator_confirm()"]
```

| `confirm_request.id`            | Кто отвечает                 | Реле M5 |
| ------------------------------- | ---------------------------- | ------- |
| `opto_in1_active` / `_inactive` | M5 авто                      | RLY3    |
| `opto_in2_active` / `_inactive` | M5 авто                      | RLY4    |
| `opto_rs_active` / `_inactive`  | M5 авто                      | RLY2    |
| `can_rx_ready`                  | M5 авто (CAN TX)             | —       |
| `can_tx_verify`                 | M5 авто (CAN RX)             | —       |
| `btn*`                          | физика, без JSON-ответа      | —       |
| всё остальное                   | оператор, prompt + countdown | —       |

**Гарантия таймаута:** `Orchestrator.run_tests()` всегда завершается ровно
одним событием `SUMMARY` — настоящим от firmware или синтетическим
(`aborted: true`), если чтение порта оборвалось по таймауту. Без этой гарантии
зависший тест блокировал бы кнопки "Выйти" и повторного запуска навсегда
(исторический баг, см. `CHANGELOG.md`).

---

## 5. Протокол M5 agent (JSON-lines)

`m5_client.py` общается с `tools/hil/m5/agent.py` напрямую (не через
`tools/hil/` pytest-окружение — у HIL pytest свой собственный путь: порт берёт
из `.env` (`HIL_M5_PORT`) и не использует `M5Client`).

Актуальный формат ответа агента:

```json
{"ok": true, "id": 1, "data": [...]}
```

Значимые детали, зафиксированные по факту сверки с `agent.py` (`grep` по
обработчикам команд):

- Поле успеха — **`ok`** (bool), не `status`. Относится ко всем командам:
  `ping`, `relay_set`, `relay_get`, `can_send`, `can_recv`.
- Ключ канала реле в `relay_set`/`relay_get` — **`ch`**, не `relay`.
- VID/PID детекта M5StampPLC настраиваются через `.env`:
  `SERVICE_M5_VID`/`SERVICE_M5_PID` (см. README, раздел «Конфигурация»).
  Рантайм-режим агента (MicroPython) отличается от ROM-режима ESP32-S3 по
  PID — при детекте ориентироваться на `just host::m5-scan`, а не на
  документацию, если она когда-либо разойдётся с кодом.

**Важно на будущее:** документация (`HIL_BENCH.md`/`HIL_HOW_TO.md`) местами не
успевает за изменениями `agent.py`. При любых будущих изменениях протокола
агента (новые команды, смена формата ответа) — сверяться напрямую через
`grep` по `tools/hil/m5/agent.py`, а не полагаться только на документацию.

---

## 6. Мониторинг соединения и разрыв сессии

`ConnectionWatcherMixin` (`screens/connection_watcher.py`) подключается к
`FlashScreen` и `DiagScreen`: каждые 1.5с проверяет, виден ли таргет на шине.

- **На `FlashScreen`** — проверка приостановлена во время активной
  прошивки/erase (обрыв обнаружит сам `flash_usb.py` subprocess).
- **На `DiagScreen`** — проверка приостановлена во время прогона тестов
  (обрыв надёжнее детектирует таймаут чтения порта внутри `Orchestrator`, не
  просто исчезновение устройства из списка).
- При срабатывании — `ConnectionLost` message → экран постит
  `FlashDone(success=False, target=None)` / `DiagDone(reason=...)` →
  `ServiceApp` разрывает сессию (`FirmwareClient.disconnect()`) и переключает
  на `WaitingScreen(disconnect_reason=...)`.
- `WaitingScreen` показывает причину возврата баннером на 4 секунды, затем
  продолжает обычный автодетект.

Архитектурное решение: **сессия никогда не восстанавливается** — после
разрыва TUI не пытается определить, вернулась ли та же плата, просто стартует
диагностику с нуля.

---

## 7. AppFrame — общий каркас экранов

`widgets/app_frame.py` — единый контейнер, который оборачивает содержимое
всех трёх основных экранов:

```css
AppFrame {
    width: 100%;
    height: 100%;
    max-width: 112;
    max-height: 35;
    border: heavy $primary;
}
```

Решает две задачи:

1. **Визуальная консистентность** — одна и та же рамка на всех экранах.
2. **Устраняет краш Textual 8.x** при mouse drag
   (`assert isinstance(content_widget.parent, Widget)`) — раньше `Screen` мог
   выступать `content_widget` напрямую; с `AppFrame` между `Screen` и
   контентом всегда есть валидный промежуточный `Widget`.

Адаптивный размер (`100%` с потолком `112×35`) — гарантирует, что элементы
управления (кнопки, таблицы) никогда не обрезаются на маленьком терминале и
не расползаются на огромном мониторе. Потолок подобран и подтверждён
визуально на скриншотах всех пяти экранов; нижняя граница по ширине
обоснована жёстко: `TestListPanel(width:38)` + `ResultsPanel(width:70)` рядом
на `DiagScreen` дают 108 + рамка = 110 — меньше сжимать уже нельзя.

---

## 8. Архитектура экранов

```mermaid
graph TB
    subgraph ServiceApp["ServiceApp (app.py)"]
        direction LR
        WS["WaitingScreen"]
        FS["FlashScreen"]
        PF["PostFlashScreen"]
        DS["DiagScreen"]
    end

    subgraph DiagInternals["DiagScreen (screens/diag/)"]
        TL["TestListPanel\ntest_list.py"]
        RP["ResultsPanel (DataTable)\nresults.py"]
        CP["ConfirmPanel\nconfirm_panel.py"]
        OR["Orchestrator\norchestrator.py"]
    end

    subgraph Clients["Клиенты"]
        FC["FirmwareClient"]
        M5["M5Client"]
        FL["Flasher"]
    end

    WS -->|"DeviceDetected(FLASHING)"| FS
    WS -->|"DeviceDetected(DIAGNOSING)"| DS
    FS -->|"FlashDone(success=True, target=FIRMWARE_TEST)"| PF
    FS -->|"FlashDone(остальное)"| WS
    PF -->|"Done"| WS
    DS -->|"DiagDone(reason)"| WS

    FS --> FL
    DS --> OR
    DS --> TL
    DS --> RP
    DS --> CP
    CP -->|"Confirmed"| DS
    OR --> FC
    OR --> M5
    WS --> FL

    FS -.->|"ConnectionWatcherMixin"| FL
    DS -.->|"ConnectionWatcherMixin"| FL
```

---

## 9. Жизненный цикл диагностической сессии

```mermaid
sequenceDiagram
    participant OP as Оператор
    participant TUI as ServiceApp
    participant WS as WaitingScreen
    participant DS as DiagScreen
    participant FW as firmware_test
    participant M5 as M5StampPLC

    OP->>TUI: запустить service_tui
    TUI->>WS: push_screen()
    WS->>WS: pyusb poll каждые 1.5 с

    OP->>FW: подключить плату USB
    WS->>TUI: DeviceDetected(DIAGNOSING)
    TUI->>FW: auto_connect() → ping→pong
    TUI->>FW: get_version()
    TUI->>M5: auto_connect() (опционально)
    TUI->>DS: switch_screen(fw_version=...)

    DS->>FW: list_tests() → TestInfo×N
    DS->>FW: get_uid()
    DS->>DS: populate (тесты НЕ выбраны по умолчанию)
    DS->>DS: ConnectionWatcherMixin: старт мониторинга

    OP->>DS: выбрать тесты / "Выбрать все" → Запустить
    DS->>FW: run_selected([...])

    loop Для каждого теста
        FW-->>DS: test_begin
        DS->>DS: ResultsPanel.set_running()

        opt progress (напр. usd)
            FW-->>DS: progress {step, status}
            DS->>DS: обновить прогресс-строку
        end

        alt HIL confirm (opto / can)
            FW-->>DS: confirm_request
            DS->>M5: relay_set() / can_send() / can_recv()
            DS->>FW: send_confirm(true/false)
        else Оператор (display / mqs)
            FW-->>DS: confirm_request
            DS->>DS: ConfirmPanel.show_operator()
            OP->>DS: OK / Нет
            DS->>FW: send_confirm(true/false)
        else Кнопки
            FW-->>DS: confirm_request
            DS->>DS: ConfirmPanel.show_buttons_hint()
            OP->>FW: физическое нажатие
        end

        FW-->>DS: test_result
        DS->>DS: ResultsPanel.set_result() + сортировка FAIL-наверх
    end

    FW-->>DS: summary
    DS->>DS: показать итог PASS / FAIL

    alt Нормальное завершение
        OP->>DS: ESC / Выйти → DiagDone()
        TUI->>WS: switch_screen()
    else Потеря USB
        DS->>DS: ConnectionLost
        TUI->>FW: disconnect()
        TUI->>WS: switch_screen(disconnect_reason=...)
    end
```

---

## 10. Версионирование firmware

`firmware_test` версионируется через CMake
(`project(firmware_test VERSION X.Y.Z)`), генерирует `version.h` через
`configure_file`. Команда протокола `get_version` (по аналогии с `get_uid`)
запрашивается один раз при подключении в `ServiceApp._connect_and_diagnose()`
и передаётся в `DiagScreen` параметром конструктора — версия не запрашивается
повторно внутри самого экрана.

Версия самого TUI (`service_tool vX.Y.Z` на `WaitingScreen`) читается
отдельно — `waiting.py::_read_app_version()` парсит `[project].version` из
`pyproject.toml` напрямую через `tomllib` (stdlib). `importlib.metadata`
сознательно не используется — проект не ставится как пакет
(`tool.uv.package = false`), метаданных может не быть.

---

## 11. Логотип (`boot_art.py`)

`LOGO_ART` — Rich-markup строка (29×21 символов, цвета `#3ca0dc` для синей
части логотипа, `white` для тёмной, `grey37` для фоновых точек), полученная
одноразовой coverage-based растеризацией `logo.png` (300×300 RGBA) через
Pillow: разбор на сетку символов с компенсацией аспекта шрифта терминала
(`ASPECT = 0.5`), классификация фона/двух цветовых групп логотипа по каналам,
плотность символа на ячейку — по доле непрозрачных пикселей
(`.::+*#`/`.::+%@`).

Сам скрипт растеризации **не сохранён в репозитории** (использовался
разово в песочнице, не входит в `pyproject.toml` TUI — новых
runtime-зависимостей `boot_art.py` не добавляет). Если логотип компании
сменится — скрипт нужно будет написать заново; логика воспроизводима (см.
абзац выше).

---

## 12. Известные грабли Textual 8.x

Зафиксировано на практике — экономит время при будущих доработках:

- **`Screen.Message` не существует.** Вложенные сообщения экранов
  наследуются от `textual.message.Message` напрямую, не от несуществующего
  атрибута `Screen.Message`.
- **`self._running` — зарезервированное имя.** `MessagePump` (предок
  `Screen`) использует это поле для своего внутреннего message loop.
  Случайное совпадение имени тихо ломает логику без исключения — в
  `DiagScreen` переименовано в `_tests_running`.
- **`row.mount(child)` сразу после `self.mount(row)` бросает `MountError`** —
  `row` ещё не прикреплён к DOM. Решение: передавать детей в конструктор
  контейнера (`Horizontal(cb, label, classes=...)`) и монтировать одним
  `mount_all()`.
- **`CSS_PATH` резолвится относительно файла класса**, не относительно корня
  проекта — постоянно расходится при рефакторинге структуры. Решение: один
  `CSS_PATH` только на `ServiceApp`, все стили в едином `app.tcss`.
- **`table.add_columns(*labels)` не принимает `width=`.** Колонка получает
  ширину по умолчанию равную длине заголовка — длинный контент обрезается
  независимо от `height` строки. Нужно использовать `add_column(label,
  width=N)` по одной колонке.
- **`DataTable.sort(*columns, key=fn)` передаёт в `key()` кортеж значений
  ячеек** (для указанных `columns`), не `row_key` и не `(row_key, row_data)`.
  Сортировка по `test_id` напрямую невозможна без парсинга содержимого ячеек,
  которые сами полностью контролируем.
- **Нет публичного API для изменения высоты уже добавленной строки.**
  `update_cell()` меняет только содержимое. Если нужно изменить `height`
  (например, под более длинный текст) — единственный надёжный путь:
  `remove_row()` + `add_row(..., height=N)`.
- **Центровка текста внутри full-width виджета не решается `Center()`.**
  `Static`/`Label` без `text-align`, растянутый на всю ширину родителя,
  прижимает текст к левому краю — `Center()` вокруг такого виджета не
  помогает (центрировать нечего, ребёнок и так 100% ширины). Нужен
  `text-align: center` в CSS на самом элементе. Обратный случай — виджет с
  "естественной" (auto) шириной — центрируется именно через `Center()`.
  Важно не путать эти два случая (`#waiting-version` — первый случай,
  `#post-flash-title`/`#post-flash-instruction` — второй).

---

## Известные открытые вопросы

- **Release-сборка firmware нестабильна** (медленное мигание — подозрение на
  проблему с FCB/clock конфигурацией в Release HAB-образе) — TUI временно
  форсирует Debug через `FIRMWARE_BUILD_TYPE`.
- **`tools/shared/m5_agent.py`** — сознательно не делался: pytest
  HIL-окружение и TUI используют независимые M5-клиенты, признано правильным
  архитектурным решением, а не техдолгом.
- Пункты плана TUI «экспорт результатов в JSON с привязкой к UID» и
  «копирование UID с экрана» — отложены, не начаты.

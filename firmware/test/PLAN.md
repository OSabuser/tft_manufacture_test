# firmware_test — План разработки

> Версия: 0.6 | Обновлён после завершения Этапа 5 (display + buttons) и архитектурных решений по Этапам 6–8.

---

## Контекст проекта

**Цель прошивки:** диагностика платы MIMXRT1052CVJ5B на сервисе (возврат по рекламации).
Запускается через BootROM (USB SDP), без предварительной прошивки загрузчика.

**Стенд:**

- Хост подключается через USB CDC ACM — единственный канал firmware_test
- HIL-тесты управляются через M5StampPLC (опционально)
- TUI-приложение оркестрирует и firmware_test (CDC), и M5 (Serial) одновременно

---

## Текущий статус

| Компонент                      | Статус | Примечание                                   |
| ------------------------------ | ------ | -------------------------------------------- |
| `bsp_usb_cdc`                  | ✅      | HIL тест пройден                             |
| firmware_test скелет           | ✅      | `main.c` + `cli.c`                           |
| Протокол v2 + test_runner      | ✅      | JSON-lines event-driven                      |
| `bsp_sdram` + `test_sdram`     | ✅      | 4 фазы: addr/data/seq/retention              |
| `bsp_qspi_flash` + `test_qspi` | ✅      | JEDEC + erase + rw + addr range              |
| `bsp_sd` + `test_usd`          | ✅      | bsp_sd + FatFS, pre_confirm, 4 шага          |
| `bsp_display` + `test_display` | ✅      | 4 цвета + ротация, hardware-verified         |
| `bsp_button` + `test_buttons`  | ✅      | 2 кнопки, physical detect, hardware-verified |
| Протокол: `list_tests`         | ⬜      | Этап 6а                                      |
| Протокол: `run_selected`       | ⬜      | Этап 6а                                      |
| `test_opto`                    | ⬜      | Этап 6б (bsp_opto ✅)                         |
| `test_can`                     | ⬜      | Этап 6в (bsp_can ✅)                          |
| `bsp_mqs` + `test_mqs`         | ⬜      | Этап 6г                                      |
| HIL pytest firmware_cdc        | ⬜      | Этап 6д                                      |
| Provisioning                   | ⬜      | Этап 7                                       |
| TUI сервисного инженера        | ⬜      | Этап 8                                       |

---

## Матрица тестов — итоговая

| ID        | Название           | Critical | HIL | Тип         | BSP                 | Статус |
| --------- | ------------------ | -------- | --- | ----------- | ------------------- | ------ |
| `sdram`   | SDRAM 32 MB        | ✅        | ❌   | self        | `bsp_sdram` ✅       | ✅      |
| `qspi`    | QSPI Flash W25Qxx  | ✅        | ❌   | self        | `bsp_qspi_flash` ✅  | ✅      |
| `usd`     | microSD (SDIO)     | ❌        | ❌   | interactive | `bsp_sd` ✅          | ✅      |
| `display` | TFT Display RGB888 | ❌        | ❌   | interactive | `bsp_display` ✅     | ✅      |
| `buttons` | Test Buttons 1/2   | ❌        | ❌   | interactive | `bsp_button` ✅      | ✅      |
| `mqs`     | MQS Audio Out      | ❌        | ❌   | interactive | `bsp_mqs` (⬜ новый) | ⬜      |
| `can`     | CAN loopback       | ❌        | ✅   | HIL         | `bsp_can` ✅         | ⬜      |
| `opto`    | Оптовходы IN1/2+RS | ❌        | ✅   | HIL         | `bsp_opto` ✅        | ⬜      |

**Убранные тесты (закрытые решения):**

- `uart_ttl` — LPUART1 dev-инструмент (MCU-Link VCOM), в сервисе не используется
- `uart_iso` — RS_RX физически тот же пин что IN в `test_opto`, избыточно

---

## Закрытые архитектурные решения

> Не пересматривать без явного запроса.

### Этапы 1–5 (ранее зафиксированные)

- **Транспорт:** USB CDC ACM — единственный канал. UART не используется в firmware_test.
- **Парсинг JSON:** без cJSON, строковый `strstr`. Входящее поле всегда `"type"` / `"cmd"`.
- **SDRAM и DCD:** SEMC инициализируется DCD до `main()`. `bsp_sdram_init()` только верифицирует.
- **QSPI-функции в ITCM:** `AT_QUICKACCESS_SECTION_CODE` + `__STARTUP_INITIALIZE_RAMFUNCTION`.
- **W25Q256/512:** dedicated 4-byte opcodes, без Enter 4-Byte Mode (0xB7).
- **bsp_button_init():** вызывается в `init()` тест-модуля, не в `main.c`.
- **Тест дисплея:** 4 цвета + 2 ротации. Таймаут confirm 15 с → FAIL.
- **Тест кнопок:** физическая детекция через `bsp_button`. Хост не отправляет JSON confirm. Таймаут 10 с → SKIP.

### Этап 6 (новые решения)

- **Разделение тестов:** `requires_hil=false` (standalone) vs `requires_hil=true` (HIL).
  TUI фильтрует HIL-тесты если M5StampPLC не подключён.
- **`list_tests`:** таргет отдаёт реестр тестов с метаданными по запросу хоста.
  TUI строит UI динамически, не хардкодит список тестов.
- **`run_selected`:** запуск произвольного подмножества тестов по списку ID.
  Порядок выполнения — как в реестре таргета, не как в запросе.
  Таргет принимает любой список без проверки `requires_hil` — ответственность на TUI.
- **TUI оркестрирует M5:** firmware_test не знает про M5. При `confirm_request`
  от HIL-теста TUI командует M5, получает результат, отправляет confirm.
- **M5 опционален:** TUI при старте пробует найти M5. Не нашёл — HIL-тесты
  недоступны (серые в UI, не входят в `run_selected`).
- **Фильтрация HIL на стороне TUI:** таргет не фильтрует по `requires_hil`.
- **MQS стерео:** MQS MIMXRT1052 требует стерео PCM-буфер. На плате выведен
  один канал. Буфер всегда стерео (L+R идентичны).
- **MQS тест:** захардкоженная мелодия 3–5 с, `confirm_request("mqs_tone")`,
  оператор слышит → OK/FAIL. `critical=false`, `requires_hil=false`.

### Этап 8 (TUI решения)

- **Прошивка — только USB SDP:** SWD недоступен сервиснику. spsdk (sdphost + blhost).
  Оператор сам переставляет перемычку BOOT — это ок, документируется.
- **TUI автодетект:** определяет подключение по VID/PID — SDP BootROM (1FC9:0130)
  или CDC firmware_test (session_start) — и показывает соответствующий экран.
- **Фреймворк TUI:** Textual (Python). Нативный async, реальные виджеты,
  работает в SSH-сессии, вписывается в uv-экосистему.
- **tools/shared/m5_agent.py:** общая M5-логика, импортируется из `tools/hil/`
  и `tools/production/`.

---

## Этап 6 — test_can + test_opto + test_mqs + протокол ← ТЕКУЩИЙ

### 6а — Расширение протокола

**Файлы:** `protocol.h`, `protocol.c`, `cli.c`, `test_runner.c`, `PROTOCOL.md`

#### Новая команда `list_tests`

```json
→ {"type":"cmd","cmd":"list_tests"}
← {"type":"test_list","tests":[
     {"id":"sdram","name":"SDRAM 32 MB","critical":true,"requires_hil":false},
     {"id":"qspi","name":"QSPI Flash W25Qxx","critical":true,"requires_hil":false},
     {"id":"usd","name":"microSD (SDIO)","critical":false,"requires_hil":false},
     {"id":"display","name":"TFT Display RGB888","critical":false,"requires_hil":false},
     {"id":"buttons","name":"Test Buttons","critical":false,"requires_hil":false},
     {"id":"mqs","name":"MQS Audio Out","critical":false,"requires_hil":false},
     {"id":"can","name":"CAN loopback","critical":false,"requires_hil":true},
     {"id":"opto","name":"Opto Inputs","critical":false,"requires_hil":true}
   ]}
```

#### Новая команда `run_selected`

```json
→ {"type":"cmd","cmd":"run_selected","tests":["sdram","qspi","display"]}
← {"type":"test_begin","id":"sdram","name":"SDRAM 32 MB","critical":true}
← {"type":"test_result","id":"sdram","status":"pass","ms":312,"detail":""}
← {"type":"test_begin","id":"qspi",...}
← {"type":"test_result","id":"qspi",...}
← {"type":"test_begin","id":"display",...}
← {"type":"test_result","id":"display",...}
← {"type":"summary","passed":3,"failed":0,"skipped":0,"overall":"pass"}
```

Если хотя бы один ID не найден в реестре:

```json
← {"ok":false,"error":"UNKNOWN_TEST"}
```

**Реализация в `test_runner.c`:**

- Новый режим `RUNNER_MODE_SELECTED`
- Статический bool-массив `g_s_selected[REGISTRY_SIZE]` — без malloc
- `test_runner_run_selected(const char **pp_ids, size_t count)` — новая публичная функция

### 6б — test_opto.c

**Файл:** `firmware/test/src/tests/test_opto.c`

6 шагов, попарно ACTIVE/INACTIVE для трёх каналов:

| Шаг | confirm_request id  | M5 действие | Проверка                         |
| --- | ------------------- | ----------- | -------------------------------- |
| 1   | `opto_in1_active`   | RLY3 ON     | `bsp_opto_read(IN1) == ACTIVE`   |
| 2   | `opto_in1_inactive` | RLY3 OFF    | `bsp_opto_read(IN1) == INACTIVE` |
| 3   | `opto_in2_active`   | RLY4 ON     | `bsp_opto_read(IN2) == ACTIVE`   |
| 4   | `opto_in2_inactive` | RLY4 OFF    | `bsp_opto_read(IN2) == INACTIVE` |
| 5   | `opto_rs_active`    | RLY2 ON     | `bsp_opto_read(RS) == ACTIVE`    |
| 6   | `opto_rs_inactive`  | RLY2 OFF    | `bsp_opto_read(RS) == INACTIVE`  |

- Init: `bsp_opto_init()` единым вызовом для всех каналов
- Верификация синхронная после confirm (M5 переключил реле до отправки `confirmed:true`)
- FAIL при несоответствии: `detail = "<id> state mismatch: expected ACTIVE got INACTIVE"`
- Таймаут: `PROTOCOL_CONFIRM_TIMEOUT_MS` (30 с) на каждый шаг

### 6в — test_can.c

**Файл:** `firmware/test/src/tests/test_can.c`

2 шага, оба направления независимо:

**Шаг 1 — RX (M5 → таргет):**

```bash
confirm_request("can_rx_ready")
→ TUI: M5.can_send(id=0x100, data=[0xDE,0xAD,0xBE,0xEF])
→ TUI: confirm(true)
→ таргет: bsp_can_receive(&frame, 500 мс)
→ верификация: frame.id==0x100, frame.data==[0xDE,0xAD,0xBE,0xEF]
→ FAIL если timeout или несовпадение
```

**Шаг 2 — TX (таргет → M5):**

```bash
bsp_can_send(id=0x200, data=[0xCA,0xFE,0xBA,0xBE], timeout=100 мс)
confirm_request("can_tx_verify")
→ TUI: M5.can_recv(timeout=500 мс) → верификация id+data
→ TUI: confirm(true) если M5 принял корректно, confirm(false) если нет
→ FAIL если confirmed=false или timeout
```

- `disableSelfReception=true` — таргет не слышит свой TX, только M5 верифицирует
- Init: `bsp_can_init(&cfg)` + `bsp_can_accept_all()`

### 6г — bsp_mqs + test_mqs.c

**Файлы:** `bsp/mqs/` (новый BSP-модуль) + `firmware/test/src/tests/test_mqs.c`

**bsp_mqs:**

- MQS требует стерео PCM (L+R), на плате один физический канал
- Буфер: всегда стерео (L == R, оба канала идентичны)
- API минимальный: `bsp_mqs_init()`, `bsp_mqs_play(buf, len)`, `bsp_mqs_stop()`, `bsp_mqs_deinit()`
- Реализация — на основе наработок (предоставит разработчик)

**test_mqs:**
- Захардкоженная мелодия, 3–5 секунд
- `confirm_request("mqs_tone")` → оператор слышит → OK/FAIL
- Таймаут: 15 с (аналогично display)
- `critical=false`, `requires_hil=false`, `pre_confirm_prompt=NULL`

### 6д — HIL pytest для firmware_test

**Файлы:**
```
tools/hil/conftest.py              ← новая фикстура firmware_cdc
tools/hil/06_test_firmware_opto.py
tools/hil/06_test_firmware_can.py
```

**Фикстура `firmware_cdc`:**

```python
@pytest.fixture(scope="module")
def firmware_cdc(m5):
    """
    Открывает USB CDC порт firmware_test.
    firmware_test уже прошит в Flash (не загружается pyOCD).
    Ждёт session_start, возвращает FirmwareCdcClient.
    """
```

**`FirmwareCdcClient`** — тонкий клиент:

- `send_cmd(cmd_dict)` — отправить JSON команду
- `wait_event(type, timeout_s)` — ждать события нужного типа
- `confirm(id, ok)` — отправить `{"type":"confirm","id":"...","confirmed":true/false}`
- `run_test(id)` — запустить тест, вернуть test_result dict

**Сценарий `06_test_firmware_can.py`:**

```python
def test_can_rx(firmware_cdc, m5):
    # Запустить тест can через firmware_test
    # При confirm_request("can_rx_ready") — M5 шлёт фрейм, затем confirm
    ...

def test_can_tx(firmware_cdc, m5):
    # При confirm_request("can_tx_verify") — M5 принимает фрейм, верифицирует
    ...
```

**Justfile:**

```bash
hil-firmware-opto  → pytest 06_test_firmware_opto.py -v
hil-firmware-can   → pytest 06_test_firmware_can.py -v
```

---

## Этап 7 — Provisioning

### Что нужно

1. Читать `OCOTP_UNIQUE_ID` через SDK `fsl_ocotp`
2. Отправить `{"type":"provision_ready","chip_uid":"AABB..."}` после `summary`
3. Ждать `{"type":"cmd","cmd":"provision_ack"}` от хоста
4. Записывать статус в Flash (первый сектор после прошивки, вне XIP)

### BSP (предварительно)

```c
/* bsp/provisioning/include/bsp/provisioning.h */
bsp_status_t bsp_prov_read_uid(uint8_t *p_uid, size_t len);  /* 8 байт из OCOTP */
```

### Открытые вопросы — Этап 7

- [ ] Что именно записывать как «пройдено»: флаг в Flash или только отправить UID?
- [ ] Нужна ли защита от повторного provisioning (write-once)?

---

## Этап 8 — TUI сервисного инженера

### Стек технологий

| Компонент     | Выбор         | Обоснование                                           |
| ------------- | ------------- | ----------------------------------------------------- |
| TUI фреймворк | **Textual**   | Нативный async, виджеты, SSH-совместим, uv-экосистема |
| Serial        | pyserial      | Уже в стеке (tools/hil)                               |
| Прошивка      | spsdk         | sdphost + blhost, уже в tools/host                    |
| Конфигурация  | python-dotenv | .env файл, совместим с существующим подходом          |

### Структура приложения

```bash
tools/production/
├── pyproject.toml          ← зависимости: textual, pyserial, spsdk, python-dotenv
├── uv.lock
├── main.py                 ← точка входа
├── app/
│   ├── tui.py              ← Textual App, экраны, layout
│   ├── firmware_client.py  ← USB CDC asyncio клиент firmware_test
│   ├── m5_client.py        ← M5 Serial клиент (импортирует tools/shared/m5_agent.py)
│   ├── flasher.py          ← USB SDP обёртка над spsdk
│   ├── orchestrator.py     ← confirm_request → M5 action → confirm response
│   └── models.py           ← TestInfo, TestResult, SessionState (dataclasses)
└── README.md

tools/shared/
└── m5_agent.py             ← общая M5-логика для hil/ и production/
```

### Два режима работы

**Режим A — Прошивка** (триггер: VID/PID 1FC9:0130 обнаружен — BootROM SDP)

```bash
┌─ Прошивка платы ─────────────────────────────────┐
│  Обнаружен BootROM (SDP режим)                     │
│                                                    │
│  Что прошить?                                      │
│  ◉ firmware_test  (диагностика)                    │
│  ○ Production     (bootloader + tft_app)           │
│                                                    │
│  Файл: [/path/to/firmware_test_hab.bin       ···]  │
│                                                    │
│  [         Прошить         ]                       │
│                                                    │
│  ████████████░░░░░░  64%   Запись во Flash...      │
└────────────────────────────────────────────────────┘
```

**Режим B — Диагностика** (триггер: session_start получен по CDC)

```bash
┌─ Диагностика платы  fw:0.1.0 ─────────────────────┐
│  M5StampPLC: ✓ подключён   │  Плата: IMXRT1052     │
├────────────────────────────────────────────────────┤
│  Выбор тестов:              │  Результаты:          │
│  ☑ SDRAM 32 MB              │  sdram    ✓ PASS      │
│  ☑ QSPI Flash               │  qspi     ✓ PASS      │
│  ☑ microSD                  │  usd      ✗ FAIL      │
│  ☑ TFT Display              │    mount failed: 5    │
│  ☑ Кнопки                   │  display  ✓ PASS      │
│  ☑ MQS Audio                │  buttons  ✓ PASS      │
│  ☑ CAN loopback  [HIL]      │  ...                  │
│  ☑ Оптовходы     [HIL]      │                       │
├────────────────────────────────────────────────────┤
│  [ Запустить выбранные ]    [ Все тесты ]          │
│  ████████████████░░░░  80%  Тест: display          │
├────────────────────────────────────────────────────┤
│  ⚠ Экран залит красным цветом?                     │
│  [ ✓ Да ]   [ ✗ Нет ]                             │
└────────────────────────────────────────────────────┘
```

### Поведение confirm_request в TUI

| Тип теста            | Источник confirm   | Действие TUI                                  |
| -------------------- | ------------------ | --------------------------------------------- |
| standalone (display) | оператор           | показать prompt, кнопки OK/FAIL, countdown    |
| standalone (buttons) | физическое нажатие | показать инструкцию, ждать test_result        |
| HIL (opto, can)      | оркестратор        | auto: M5 action → confirm (оператор не видит) |

HIL confirm полностью автоматический — оператор видит только прогресс, не интерактивный prompt.

### Конфигурация (.env)

```ini
# Существующие переменные (tools/hil/.env):
HIL_VCOM_PORT=/dev/ttyACM0
HIL_M5_PORT=/dev/ttyACM1

# Новые переменные для production TUI:
SERVICE_CDC_PORT=AUTO            # AUTO = автодетект по session_start
SERVICE_M5_PORT=AUTO             # AUTO = автодетект, пусто = без M5
FIRMWARE_TEST_BIN=build/Release/firmware_test_hab.bin
PRODUCTION_BIN_BOOT=build/Release/bootloader_hab.bin
PRODUCTION_BIN_APP=build/Release/tft_app_hab.bin
```

### Запуск

```bash
just host::service-tui           # запустить TUI сервисного инженера
just host::service-flash <bin>   # прошить без TUI (для автоматизации)
```

### Процесс работы сервисника

**Диагностика (firmware_test уже в Flash):**

```bash
1. Плата в нормальном режиме (BOOT_MOD_1 → GND)
2. Подключить USB к сервисному ПК
3. just host::service-tui  →  TUI обнаружил session_start  →  Режим B
4. Выбрать тесты → Запустить → Смотреть результаты
```

**Перепрошивка (нужна новая версия firmware_test или production):**
```
1. Перемычка BOOT_MOD_1 → 3V3
2. Reset, подключить USB
3. TUI обнаружил 1FC9:0130  →  Режим A
4. Выбрать бинарь → Прошить
5. Перемычка BOOT_MOD_1 → GND → Reset  →  TUI переходит в Режим B
```

---

## Порядок реализации

```
✅ Этап 1  протокол v2 + runner
✅ Этап 2  bsp_sdram + test_sdram
✅ Этап 3  bsp_qspi_flash + test_qspi
✅ Этап 4  bsp_sd + test_usd
✅ Этап 5  display + buttons

⬜ Этап 6а  протокол: list_tests + run_selected           ← СЛЕДУЮЩИЙ ШАГ
⬜ Этап 6б  test_opto.c + hardware верификация
⬜ Этап 6в  test_can.c + hardware верификация
⬜ Этап 6г  bsp_mqs + test_mqs.c (после получения наработок)
⬜ Этап 6д  HIL pytest: firmware_cdc фикстура
⬜ Этап 6е  HIL pytest: 06_test_firmware_opto.py
⬜ Этап 6ж  HIL pytest: 06_test_firmware_can.py

⬜ Этап 7   Provisioning (OCOTP UID + Flash-флаг)

⬜ Этап 8а  tools/production/ скелет + models + clients
⬜ Этап 8б  orchestrator + базовый Textual UI (список тестов, запуск, результаты)
⬜ Этап 8в  Экран прошивки (flasher + SDP автодетект)
⬜ Этап 8г  Provisioning в TUI
⬜ Этап 8д  tools/shared/m5_agent.py (рефакторинг общей M5-логики)

⬜ Этап 9   Параллельно: обновить README + DEV_ARCH.md под финальную архитектуру
```

---

## Зависимости между этапами

```bash
6а (протокол) → 6б (opto) → 6в (can) → 6г (mqs)
                                       ↓
                              6д (conftest) → 6е (opto pytest) → 6ж (can pytest)
                                                                        ↓
                                                               7 (provisioning)
                                                                        ↓
                                                               8 (TUI)
```
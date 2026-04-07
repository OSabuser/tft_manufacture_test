# firmware_test — Plan of Development

> Документ для нового треда. Содержит все принятые решения, текущий статус и
> пошаговый план дальнейшей разработки.
> Версия: 0.2 | Обновлён после реализации скелета + bsp_usb_cdc.

---

## Контекст проекта

**Цель прошивки:** входной контроль платы MIMXRT1052CVJ5B на производстве.
Запускается через BootROM (USB SDP), без предварительной прошивки загрузчика.

**Стенд:**
- Хост подключается через USB CDC ACM (J2) — единственный канал
- Тесты с внешними сигналами управляются через M5StampPLC (реле, оптовходы)
- Отдельный компьютер-сервер запускает pytest

---

## Статус на момент создания документа

| Компонент | Статус | Примечание |
|---|---|---|
| `bsp_usb_cdc` | ✅ Готов | HIL тест пройден (`05_test_usb_cdc.py`) |
| `firmware_test` скелет | ✅ Готов | `main.c` + `cli.c` + PING работает |
| Host-тест CLI | ✅ Готов | `test_cli.c`, 8 тестов, зелёные |
| `bsp_sdram` | ⬜ Не начат | |
| `bsp_qspi` | ⬜ Не начат | |
| `bsp_usd` | ⬜ Не начат | |
| Протокол v2 | ⬜ Не начат | Эволюция от cmd/ok к event-driven |
| Test runner | ⬜ Не начат | |
| Provisioning | ⬜ Не начат | |

### Закрытые архитектурные решения

> Не пересматривать без явного запроса.

- **[DECISION] Транспорт:** USB CDC ACM — единственный канал. UART не используется в firmware_test.
- **[DECISION] Парсинг JSON:** без cJSON, строковый `strstr`. Входящее поле всегда `"cmd"` / `"type"`.
- **[DECISION] SDRAM и DCD:** SEMC инициализируется DCD до `main()`. `bsp_sdram_init()` только верифицирует.
- **[DECISION] SDRAM тест — не HIL ELF:** тест прогоняется командами через firmware_test, не отдельным ELF.
- **[DECISION] IR и RTC:** не реализуются.

---

## Протокол v2 — решение, требующее принятия в новом треде

Текущий скелет использует упрощённый протокол:
```json
{"cmd":"PING"} → {"ok":true,"result":"PONG"}
```

TODO.md описывает расширенный протокол с типами событий:
```json
{"type":"session_start","fw":"0.1.0","target":"IMXRT1052","uptime_ms":0}
{"type":"test_begin","id":"sdram","name":"SDRAM 32MB","critical":true}
{"type":"test_result","id":"sdram","status":"pass","ms":312}
{"type":"summary","passed":7,"failed":0,"overall":"pass"}
```

**Вопросы для обсуждения в треде:**

1. Переходить ли на v2 сразу или итерационно (сначала SDRAM с cmd/ok, потом рефакторинг)?
2. Как обрабатывать `confirm_request` (display test) в cli.c — отдельный тип входящего сообщения?
3. `session_start` — посылать ли при каждом `READY` или только по команде `START_SESSION`?

**Рекомендация:** реализовать v2 до добавления первого теста, иначе рефакторинг CLI затронет уже написанные тест-модули.

---

## Структура файлов — целевое состояние
```
firmware/test/
├── CMakeLists.txt
├── README.md
└── src/
    ├── main.c                    # ✅ готов
    ├── cli.h / cli.c             # ✅ готов (v1), требует эволюции до v2
    ├── protocol.h / protocol.c   # ⬜ новый: сериализация ответов
    ├── test_runner.h / .c        # ⬜ новый: реестр + sequencer
    ├── provisioning.h / .c       # ⬜ новый: chip UID + provision_ack
    └── tests/
        ├── test_sdram.h / .c     # ⬜
        ├── test_qspi.h / .c      # ⬜
        ├── test_usd.h / .c       # ⬜
        ├── test_display.h / .c   # ⬜ интерактивный (кнопки)
        ├── test_can.h / .c       # ⬜ HIL (M5StampPLC)
        ├── test_uart.h / .c      # ⬜ HIL TTL + ISO
        └── test_opto.h / .c      # ⬜ HIL (M5StampPLC)

bsp/
├── sdram/                        # ⬜ CMakeLists.txt + sdram.c (структура есть)
├── qspi/                         # ⬜ новый модуль
└── usd/                          # ⬜ новый модуль
```

---

## Матрица тестов

| ID | Название | Тип | Critical | HIL (M5) | HIL ELF | BSP | Статус |
|---|---|---|---|---|---|---|---|
| — | PING | cmd | — | — | — | — | ✅ |
| `sdram` | SDRAM 32MB | self | ✅ | ❌ | ❌ | `bsp_sdram` | ⬜ |
| `qspi` | QSPI Flash | self | ✅ | ❌ | ❌ | `bsp_qspi` | ⬜ |
| `usd` | uSD (SDIO) | self | ✅ | ❌ | ❌ | `bsp_usd` | ⬜ |
| `display` | Display RGB888 | interactive | ❌ | ❌ | ❌ | существующий BSP | ⬜ |
| `can` | CAN | HIL | ❌ | ✅ | ❌ | `bsp_can` ✅ | ⬜ |
| `uart_ttl` | UART TTL | HIL | ❌ | ✅ | ❌ | `bsp_uart_host` ✅ | ⬜ |
| `uart_iso` | UART ISO +24V | HIL | ❌ | ✅ | ❌ | уточнить | ⬜ |
| `opto` | Opto-in +24V | HIL | ❌ | ✅ | ❌ | `bsp_opto` ✅ | ⬜ |

**Колонка "HIL ELF":** отдельная RAM-прошивка через pyOCD. Для firmware_test тестов — не нужна, тесты идут через USB CDC.

**Колонка "HIL (M5)":** нужна ли M5StampPLC для управления внешними сигналами.

---

## Пошаговый план

### Этап 0 — Вопросы, требующие ответа до кода

Обсудить в начале треда:

- Протокол v2: переходить сейчас или после SDRAM?
- `uart_iso`: есть отдельный BSP модуль или это тот же `bsp_uart_host` с другими параметрами?
- `usd`: карта вставлена постоянно на плате или оператор вставляет перед тестом?
- QSPI: какой конкретно чип (W25Q128?), есть ли уже sdk_flexspi таргет в CMake?

---

### Этап 1 — Протокол v2 + Test runner скелет

**Цель:** эволюция cli.c → protocol.c + test_runner.c. После этого этапа добавление каждого теста — одна строка в реестре.

**Файлы:**
```
firmware/test/src/
├── cli.c          ← упрощается: только IO (read/write/buffer)
├── protocol.h/.c  ← новый: сериализация всех типов сообщений
├── test_module.h  ← новый: интерфейс тест-модуля
└── test_runner.h/.c ← новый: реестр + sequencer
```

**Интерфейс тест-модуля (`test_module.h`):**
```c
typedef enum { TEST_STATUS_PASS = 0, TEST_STATUS_FAIL, TEST_STATUS_SKIP } test_status_t;

typedef struct {
    test_status_t status;
    uint32_t      duration_ms;
    char          detail[96];
} test_result_t;

typedef struct {
    const char   *id;
    const char   *name;
    bool          critical;
    bool          requires_hil;
    void         (*init)(void);
    test_result_t (*run)(void);
    void         (*deinit)(void);
} test_module_t;
```

**Сериализация (protocol.h):** функции `protocol_send_session_start()`,
`protocol_send_test_begin()`, `protocol_send_test_result()`,
`protocol_send_summary()`, `protocol_send_confirm_request()` — все через `cli_send()`.

**Команды хоста v2:**

| Входящий тип | Поле | Действие |
|---|---|---|
| `cmd` | `"run_all"` | Запустить все тесты по реестру |
| `cmd` | `"run"` + `"id"` | Запустить один тест |
| `cmd` | `"ping"` | `{"type":"pong"}` |
| `confirm` | `"id"` + `"confirmed"` | Ответ оператора на display тест |

**Host-тесты:** расширить `test_cli.c` + добавить `test_protocol.c` (Категория A).

---

### Этап 2 — `bsp_sdram`

API уже описан в `firmware_test_plan.md` (закрытое решение):
```c
bsp_sdram_status_t bsp_sdram_init(void);
bsp_sdram_status_t bsp_sdram_test_fast(bsp_sdram_result_t *result);
bsp_sdram_status_t bsp_sdram_test_full(bsp_sdram_result_t *result);
```

**Файлы:**
```
bsp/sdram/
├── CMakeLists.txt              ← создать
├── include/bsp/sdram.h        ← по spec из firmware_test_plan.md
└── src/sdram.c
```

**Тест модуль firmware_test:**
```
firmware/test/src/tests/test_sdram.c
```

Команды через реестр: `run` + `id: "sdram"`. Отдельной HIL ELF нет.

**HIL pytest (`tools/hil/test_sdram.py`):** подключается к firmware_test через USB CDC.
Фикстура `cdc_firmware_test` — firmware_test прошит в Flash, pytest открывает CDC порт.

**Just рецепты:** `hil-sdram` (без `--slow`), `hil-sdram-full` (с `@pytest.mark.slow`).

---

### Этап 3 — `bsp_qspi` + QSPI тест

**Аппаратный контекст:** W25Q64FVSSIG (из схемы, 8MB SPI NOR Flash), интерфейс FlexSPI.

**API:**
```c
bsp_qspi_status_t bsp_qspi_init(void);
bsp_qspi_status_t bsp_qspi_read_jedec_id(uint8_t *manufacturer, uint16_t *device_id);
bsp_qspi_status_t bsp_qspi_test(bsp_qspi_result_t *result);  /* erase sector + write + verify */
```

**Что проверяет тест:**

1. JEDEC ID совпадает с ожидаемым для W25Q64 (`0xEF`, `0x4017`)
2. Erase тестового сектора (последний сектор, чтобы не трогать прошивку)
3. Write + Read + Compare 256 байт

**Нет HIL ELF, нет M5.** Тест полностью самостоятельный.

**Вопрос перед началом:** проверить есть ли `sdk_flexspi` таргет в `sdk/CMakeLists.txt`.

---

### Этап 4 — `bsp_usd` + uSD тест

**Аппаратный контекст:** SDMMC (uSD слот), интерфейс USDHC. SDK таргет `sdk_usdhc` есть в матрице.

**Стратегия:** использовать FatFS из SDK middleware (уже vendored в `middleware/fatfs/`).

**API:**
```c
bsp_usd_status_t bsp_usd_init(void);           /* USDHC init + mount FAT */
bsp_usd_status_t bsp_usd_test(bsp_usd_result_t *result);  /* write + read + verify */
bsp_usd_status_t bsp_usd_deinit(void);         /* unmount */
```

**Поведение при отсутствии карты:** `TEST_STATUS_SKIP` (критически важно для
производственного прогона — карта может быть не вставлена).

**Вопрос:** карта вставлена постоянно или оператор вставляет? Если оператор — нужен `confirm_request` перед тестом.

---

### Этап 5 — Display тест (интерактивный)

**Что проверяем:** RGB888 интерфейс + подсветка + реакция оператора через кнопки.

**Последовательность:**
```
firmware → LCD: залить R (красный)
firmware → хост: {"type":"confirm_request","id":"display_red","timeout_ms":15000}
оператор: нажать кнопку PASS (Test_But_1) или FAIL (Test_But_2)
firmware → хост: {"type":"confirm_ack","id":"display_red","confirmed":true/false}
повторить для G, B, W
итоговый результат = AND всех подтверждений
```

**Особенности:**

- Одновременно тестируются кнопки (`bsp_button` уже есть с HIL тестами)
- Таймаут 15 с → `TEST_STATUS_SKIP`
- Маркер `@pytest.mark.interactive` в pytest — не входит в `hil-run`

**HIL pytest:** `tools/hil/test_display.py` с `_operator_prompt()` через `/dev/tty`.

---

### Этап 6 — HIL тесты: CAN, UART, Opto в firmware_test

Эти BSP модули уже реализованы и имеют отдельные HIL ELF тесты.
Задача этапа — **интегрировать их в firmware_test** как тест-модули,
запускаемые через протокол v2.

#### 6.1 CAN (`bsp_can` ✅)

- Стенд: M5StampPLC подключён к CAN шине платы через интерфейсную плату
- Тест: M5 посылает CAN фрейм → плата принимает → сравниваем

#### 6.2 UART TTL (`bsp_uart_host` ✅)

- Стенд: M5 UART ↔ UART TTL платы (loopback или echo)
- Уточнить: какой UART порт на плате (LPUART1 занят MCU-Link, какой свободен?)

#### 6.3 UART ISO

- Уточнить наличие отдельного BSP модуля или это конфигурация `bsp_uart_host`
- +24V уровни через интерфейсную плату

#### 6.4 Opto (`bsp_opto` ✅)

- Стенд: M5 реле → оптовходы EXT_IN1, EXT_IN2, RS_RX
- Логика уже отработана в `02_test_opto.py` (HIL ELF)
- Переиспользовать: тот же M5 агент, другой транспорт (USB CDC вместо UART)

**Важно для всех HIL тестов этапа 6:** pytest для firmware_test использует
`cdc_firmware_test` фикстуру (USB CDC), а не `uart_<n>` (UART + pyOCD).
M5StampPLC управляет сигналами так же, как в существующих HIL ELF тестах.

---

### Этап 7 — Provisioning

**Источник UID:** OCOTP регистры через `OCOTP_GetFuseData()` (NXP HAL).
```c
void provisioning_run(provision_info_t *out);
```

**Поток:**
```
firmware → хост: {"type":"provision_ready","chip_uid":"A3F2...","fw":"0.1.0"}
хост → БД: uid ↔ fw_version (логика на хосте)
хост → firmware: {"type":"cmd","cmd":"provision_ack","fw":"1.0.0","bootloader":"1.0.0"}
firmware → хост: {"type":"provision_done","recorded":true}
```

**Запускается только при `overall == pass`.** В pytest отдельная фикстура
`provision_firmware_test`.

---

## Зависимости между этапами
```
Этап 1 (протокол v2 + runner)
    ├── Этап 2 (bsp_sdram)
    │       └── HIL: test_sdram.py
    ├── Этап 3 (bsp_qspi)
    │       └── HIL: test_qspi.py
    ├── Этап 4 (bsp_usd)
    │       └── HIL: test_usd.py
    ├── Этап 5 (display, interactive)
    │       └── HIL: test_display.py (@pytest.mark.interactive)
    └── Этап 6 (CAN + UART + Opto)
            └── HIL: test_can.py, test_uart.py, test_opto.py

Этап 7 (provisioning) → зависит от всех предыдущих
```

---

## BSP модули — итог

| BSP | Статус | Нужен HIL ELF | Нужен в firmware_test |
|---|---|---|---|
| `bsp_usb_cdc` | ✅ | ✅ (есть) | ✅ (есть) |
| `bsp_led` | ✅ | ❌ | ✅ |
| `bsp_tick` | ✅ | ❌ | ✅ |
| `bsp_button` | ✅ | ✅ (есть) | ✅ (display тест) |
| `bsp_opto` | ✅ | ✅ (есть) | ✅ этап 6 |
| `bsp_can` | ✅ | ✅ (есть) | ✅ этап 6 |
| `bsp_uart_host` | ✅ | ✅ (есть) | ✅ этап 6 |
| `bsp_sdram` | ⬜ | ❌ | ✅ этап 2 |
| `bsp_qspi` | ⬜ | ❌ | ✅ этап 3 |
| `bsp_usd` | ⬜ | ❌ | ✅ этап 4 |

---

## Контекст для нового треда — что передать
```
Системный промпт: тот же (роль + правила).

Приложить файлы:
  - firmware_test_plan.md (обновлённый)
  - usb_cdc.md
  - firmware/test/src/main.c     (текущий)
  - firmware/test/src/cli.h/.c   (текущий)
  - firmware/test/CMakeLists.txt (текущий)
  - tests/host/CMakeLists.txt
  - tests/host/cli/test_cli.c
  - этот план (PLAN.md)

Первый вопрос нового треда:
  "Начинаем Этап 1 — протокол v2 и test_runner.
   Ответы на открытые вопросы: [...]"
```

---

## Открытый вопрос: ПО на стороне хоста для производственного прогона

### Контекст и разделение ответственности

Принципиально важно разделить два окружения:

| Окружение | Кто запускает | Инструмент | Что тестирует |
|---|---|---|---|
| **Разработка** | Разработчик, локальный ПК | pytest (`just host::hil-*`) | Отдельные BSP модули через HIL ELF + UART |
| **Производство / Сервис** | Оператор, сервер | ??? | Плата целиком через firmware_test + USB CDC |

Это два **принципиально разных** use case с разными требованиями к UX,
надёжности и изоляции. Смешивать их в одном pytest-прогоне нельзя.

---

### Почему "просто pytest" недостаточно для производства

Производственный прогон отличается от HIL тестов разработчика по нескольким осям:

**Оператор — не разработчик.** Он не читает pytest output в терминале.
Ему нужно видеть: какой тест сейчас идёт, прошёл или нет, что делать дальше
(вставить карту, посмотреть на дисплей, нажать кнопку).

**Последовательность фиксирована.** Прогон всегда идёт по реестру:
flash → READY → run_all → summary → provisioning. Никакого выбора тестов.

**Результат — не лог, а запись в БД.** `chip_uid` + результат + версия прошивки
должны сохраняться. pytest ничего не знает о БД.

**Интерактивные тесты** (display) требуют управляемого диалога с оператором,
а не `input()` в терминале с флагом `-s`.

---

### Варианты хостового ПО — анализ

#### Вариант A: pytest + плагины + HTML отчёт

Обернуть весь прогон в pytest, добавить `pytest-html` для отчётов,
интерактивные тесты через `@pytest.mark.interactive` с `-s`.

**Плюсы:** минимум нового кода, знакомый инструмент.

**Минусы:** оператор смотрит в терминал; интерактивность через `/dev/tty` хрупкая;
provisioning (запись в БД) — костыль в фикстуре; нет живого статуса
"тест N из M идёт X секунд". Для производства неприемлемо.

#### Вариант B: TUI — Python + Textual / Rich

Самостоятельное Python-приложение с текстовым интерфейсом.
Управляет всем: flash через spsdk, M5StampPLC, USB CDC, provisioning, БД.
pytest не используется как runner — только как библиотека для assert-логики
(или вообще не используется).

**Плюсы:** полный контроль над UX; живой прогресс; чёткие диалоги оператора;
нативная запись в БД; изолировано от HIL тестов разработчика полностью.

**Минусы:** значительный объём разработки хостового ПО;
нужен отдельный репозиторий или директория `tools/production/`.

#### Вариант C: pytest как backend, TUI/GUI как frontend

pytest запускается программно через `pytest.main()` или subprocess,
результаты передаются через JSON reporter (`pytest-json-report`) в
отдельное GUI/TUI приложение которое их отображает.

**Плюсы:** переиспользуем pytest инфраструктуру (фикстуры, параллелизм).

**Минусы:** архитектурно сложно; интерактивные тесты всё равно требуют
кастомного канала; два процесса вместо одного.

#### Вариант D: pytest только для разработки, отдельный runner для производства

**Разработка:** pytest (`just host::hil-*`) — тесты отдельных BSP модулей
через HIL ELF. Остаётся как есть.

**Производство:** отдельное Python-приложение (`tools/production/run.py`)
без pytest. Использует те же низкоуровневые библиотеки
(`pyserial`, `spsdk`, M5 агент) но со своим runner-ом и TUI.

Тест-логика на стороне прошивки (в `test_runner.c`) — единственная
точка истины. Хостовое ПО только отправляет команды и интерпретирует события.

**Это рекомендуемый вариант** — наиболее чистое разделение.

---

### Рекомендуемая архитектура (Вариант D, детально)
```
tools/
├── hil/                          # СУЩЕСТВУЮЩИЙ — разработчик, локальный ПК
│   ├── conftest.py               # pytest фикстуры
│   ├── 01_test_uart.py           # HIL тесты отдельных BSP модулей
│   ├── 02_test_opto.py
│   └── ...
│
└── production/                   # НОВЫЙ — производство / сервис
    ├── pyproject.toml            # отдельное окружение uv
    ├── run.py                    # точка входа: python run.py
    ├── runner/
    │   ├── flasher.py            # spsdk: flash firmware_test.elf
    │   ├── cdc_client.py         # USB CDC: send cmd, recv events
    │   ├── m5_client.py          # переиспользовать tools/hil/m5/
    │   ├── test_sequence.py      # порядок: flash→READY→run_all→provision
    │   └── db.py                 # запись chip_uid + результатов
    └── ui/
        ├── tui.py                # Textual TUI: живой прогресс + диалоги
        └── report.py             # HTML / JSON отчёт после прогона
```

**Поток производственного прогона:**
```
Оператор запускает: python tools/production/run.py
    │
    ├── flasher.py: spsdk → flash firmware_test.elf → Reset
    ├── cdc_client: ждёт {"type":"session_start",...}
    ├── TUI: показывает "Подключение... ОК"
    │
    ├── cdc_client: → {"type":"cmd","cmd":"run_all"}
    │
    ├── [цикл событий]
    │   ├── {"type":"test_begin","id":"sdram"} → TUI: "SDRAM... ⏳"
    │   ├── {"type":"test_result","id":"sdram","status":"pass"} → TUI: "SDRAM ✅ 47ms"
    │   ├── {"type":"confirm_request","id":"display_red"} →
    │   │       TUI: диалог оператора "Экран красный? [PASS/FAIL]"
    │   │       cdc_client: → {"type":"confirm","id":"display_red","confirmed":true}
    │   └── ...
    │
    ├── {"type":"summary","overall":"pass"} → TUI: финальный результат
    ├── {"type":"provision_ready","chip_uid":"..."} →
    │       db.py: записать в БД
    │       cdc_client: → {"type":"cmd","cmd":"provision_ack",...}
    └── TUI: "Плата принята ✅ | UID: A3F2..." | печать этикетки?
```

---

### Изоляция от HIL тестов разработчика

| Аспект | HIL тесты (разработка) | Production runner |
|---|---|---|
| Инструмент | pytest | Самостоятельный Python runner |
| Директория | `tools/hil/` | `tools/production/` |
| Окружение uv | `tools/hil/pyproject.toml` | `tools/production/pyproject.toml` |
| Just рецепты | `just host::hil-*` | `just host::production-run` |
| Прошивка | HIL ELF в RAM (pyOCD) | firmware_test в Flash (spsdk) |
| Транспорт | UART CLI (pyserial) | USB CDC JSON-lines |
| UX | Терминал / pytest output | TUI (Textual) |
| БД | Нет | Да |
| M5StampPLC | Да (реле для сигналов) | Да (те же реле) |
| CI | Да (автоматический) | Нет |

Общий код (M5 агент, низкоуровневый spsdk wrapper) можно вынести в
`tools/shared/` и подключать как локальный пакет в обоих `pyproject.toml`.

---

### Открытые вопросы для обсуждения

- [ ] **TUI библиотека:** Textual (современный, богатый) или Rich (проще, достаточно)?
      Или вообще минималистичный вывод без TUI фреймворка на первой итерации?
- [ ] **БД:** SQLite локально на сервере или REST API на внешний сервис?
- [ ] **Этикетка:** нужна ли автоматическая печать после provisioning?
- [ ] **Несколько стендов:** один сервер управляет несколькими платами параллельно
      или всегда одна плата?
- [ ] **Первая итерация:** допустимо ли начать с простого `run.py` без TUI
      (plain print + input) и добавить TUI позже?

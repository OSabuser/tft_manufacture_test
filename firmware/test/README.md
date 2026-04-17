# firmware_test

> Диагностическая прошивка для плат **MIMXRT1052CVJ5B**, вернувшихся по
> рекламации. Запускается сервисным инженером через USB CDC ACM без
> предварительной прошивки загрузчика.
>
> Версия прошивки: `0.1.0` | Протокол: v2

---

## Содержание

- [Быстрый старт](#быстрый-старт)
- [Архитектура](#архитектура)
  - [Стенд](#стенд)
  - [Модульная структура](#модульная-структура)
  - [Граф зависимостей](#граф-зависимостей)
  - [State machine test_runner](#state-machine-test_runner)
- [Протокол v2](#протокол-v2)
  - [Транспорт](#транспорт)
  - [Жизненный цикл сессии](#жизненный-цикл-сессии)
  - [Команды хоста → таргет](#команды-хоста--таргет)
  - [События таргета → хост](#события-таргета--хост)
  - [Ошибки протокола](#ошибки-протокола)
  - [Интерактивные тесты](#интерактивные-тесты)
- [Матрица тестов](#матрица-тестов)
- [Как добавить новый тест](#как-добавить-новый-тест)
- [Host unit-тесты](#host-unit-тесты)
- [Версионирование](#версионирование)

---

## Быстрый старт

### 1. Сборка (devcontainer)

```bash
just build::build-firmware-test-debug
just build::hab-firmware-test-debug
```

### 2. Прошивка (хост)

```bash
# Перевести плату в SDP-режим: BOOT_MOD_1 → 3V3 → Reset
just host::flash-test-debug

# Или через SWD (power cycle после)
just host::flash-swd-test-debug
```

### 3. Подключение

Подключить USB к разъёму **J2** (USB CDC ACM). Открыть любой терминал:

```bash
# macOS
screen /dev/cu.usbmodemXXXX

# Linux
screen /dev/ttyACM0
```

### 4. Работа с прошивкой

После подключения таргет сразу присылает:

```json
{"type":"session_start","fw":"0.1.0","target":"IMXRT1052","uptime_ms":0}
```

Проверка связи:

```json
→ {"type":"cmd","cmd":"ping"}
← {"type":"pong"}
```

Запуск одного теста:

```json
→ {"type":"cmd","cmd":"run","id":"sdram"}
← {"type":"test_begin","id":"sdram","name":"SDRAM 32 MB","critical":true}
← {"type":"test_result","id":"sdram","status":"pass","ms":312,"detail":""}
```

Запуск всех тестов:

```json
→ {"type":"cmd","cmd":"run_all"}
← ... (события каждого теста) ...
← {"type":"summary","passed":7,"failed":0,"skipped":1,"overall":"pass"}
```

### 5. Host unit-тесты (devcontainer)

```bash
just build::test-host
```

---

## Архитектура

### Стенд

```bash
[Хост-ПК сервисного инженера]
        │  USB CDC ACM (J2) — единственный канал
        │  JSON-lines, 1 строка = 1 сообщение
        ▼
[Плата MIMXRT1052 с firmware_test]
        │  GPIO / LPUART / SEMC / FlexSPI / USDHC
        ▼
[Периферия: SDRAM, QSPI Flash, uSD, Display, Кнопки, CAN, UART, Opto]
        ▲
[M5StampPLC — управление внешними сигналами для HIL тестов]
        (реле → EXT_IN1/IN2, RS_RX, CAN, UART echo)
```

**Принцип разделения ответственности:**

- Вся тест-логика живёт **на таргете** (`test_runner.c`, `tests/*.c`).
- Хост — тонкий клиент: отправляет команды, отображает события, управляет
  интерактивными шагами через `confirm`.
- Тесты **атомарны**: инженер запускает один тест или все сразу — порядок
  не фиксирован.

---

### Модульная структура

```bash
firmware/test/
├── CMakeLists.txt
└── src/
    ├── main.c              — инициализация BSP, главный цикл
    │
    ├── cli.h / cli.c       — IO-слой
    │                         буферизация строк, парсинг "type",
    │                         диспатч на test_runner / protocol
    │
    ├── protocol.h / .c     — сериализация исходящих событий
    │                         все protocol_send_*() → cli_send()
    │
    ├── test_module.h       — интерфейс тест-модуля
    │                         test_module_t, test_result_t,
    │                         confirm_params_t, test_status_t
    │
    ├── test_runner.h / .c  — реестр + state machine
    │                         IDLE → PRE_CONFIRM → RUNNING → IDLE
    │                         test_runner_wait_confirm() для display
    │
    └── tests/
        ├── test_sdram.c    — SDRAM 32 MB (self)
        ├── test_qspi.c     — QSPI Flash 8 MB (self)
        ├── test_usd.c      — uSD SDIO (interactive)
        ├── test_display.c  — Display RGB888 (interactive)
        ├── test_buttons.c  — Test_But_1/2 (interactive)
        ├── test_can.c      — CAN loopback (HIL)
        ├── test_uart_ttl.c — UART TTL (HIL)
        ├── test_uart_iso.c — UART ISO / RS_RX Opto (HIL)
        └── test_opto.c     — Opto-in EXT_IN1/IN2 (HIL)
```

---

### Граф зависимостей

```bash
main.c
  ├── bsp_board          (тактирование, MPU, кэш, пины)
  ├── bsp_tick           (SysTick 1 мс)
  ├── bsp_led            (LED_HEARTBEAT, LED_APP)
  ├── bsp_usb_cdc        (USB CDC ACM, единственный транспорт)
  ├── cli.c
  │     └── bsp_usb_cdc  (read / write)
  │     └── protocol.c   (send_error, send_pong)
  │     └── test_runner.c (run_single, run_all, on_confirm)
  ├── protocol.c
  │     └── cli.c        (cli_send)
  │     └── bsp_tick     (bsp_tick_get_ms — для uptime)
  └── test_runner.c
        └── protocol.c   (все protocol_send_*)
        └── bsp_tick     (bsp_tick_get_ms — таймауты confirm)
        └── bsp_usb_cdc  (bsp_usb_cdc_poll — в wait_confirm)
        └── cli.c        (cli_process — в wait_confirm)
        └── tests/*.c    (тест-модули через реестр)
```

**BSP-зависимости тест-модулей:**

| Тест | BSP модуль |
|---|---|
| `test_sdram` | `bsp_sdram` |
| `test_qspi` | `bsp_qspi` |
| `test_usd` | `bsp_usd` |
| `test_display` | существующий display BSP |
| `test_buttons` | `bsp_button` ✅ |
| `test_can` | `bsp_can` ✅ |
| `test_uart_ttl` | `bsp_uart_host` ✅ |
| `test_uart_iso` | `bsp_opto` (rs_as_gpio=true) ✅ |
| `test_opto` | `bsp_opto` ✅ |

---

### State machine test_runner

```bash
             cmd: run / run_all
                    │
                    ▼
  ┌─────────────────────────────────────┐
  │             IDLE                    │◄──────────────────────────┐
  │  Ждём команду от хоста              │                           │
  └──────────────────┬──────────────────┘                           │
                     │                                              │
        pre_confirm_prompt != NULL?                                 │
                     │                                              │
          YES        │         NO                                   │
          ▼          │          ▼                                   │
  ┌───────────────┐  │  ┌──────────────────────────────────────┐   │
  │  PRE_CONFIRM  │  │  │  RUNNING                             │   │
  │               │  │  │  protocol_send_test_begin()          │   │
  │  confirm_req  │  │  │  mod->init()   (если задан)          │   │
  │  отправлен,   │  │  │  result = mod->run()  ← блокирует   │   │
  │  ждём JSON    │  │  │  mod->deinit() (если задан)          │   │
  │  от хоста     │  └─►│  protocol_send_test_result()         │   │
  └──────┬────────┘     └──────────────────┬───────────────────┘   │
         │                                 │                        │
    confirmed=true ──────────────────────► │                        │
    confirmed=false → SKIP                 │                        │
    timeout → SKIP                         │                        │
                                           │                        │
                              run:  IDLE ──┘                        │
                              run_all:  следующий тест в реестре ───┘
                              run_all done: protocol_send_summary()
```

**Ключевые свойства state machine:**

- `RUNNING` — защита от ложного `is_busy()==false` во время blocking `run()`.
  Пока тест выполняется, новые команды получают `BUSY`.
- `test_runner_wait_confirm()` — вызывается из `run()` интерактивных тестов
  (display). Внутри polling loop: `bsp_usb_cdc_poll()` + `cli_process()`.
  USB-стек остаётся живым, confirm приходит без возврата в главный цикл.
- `critical=true` + `FAIL` в `run_all` → все оставшиеся тесты получают
  `SKIP` немедленно, `summary.overall = "fail"`.

---

## Протокол v2

### Транспорт

| Параметр | Значение |
|---|---|
| Интерфейс | USB CDC ACM, разъём J2 |
| Кодировка | UTF-8 |
| Фреймирование | JSON-lines: одна строка = одно сообщение, завершается `\n` |
| Максимальная длина строки | 128 байт включая `\n` |
| CR+LF | Принимается (таргет отбрасывает `\r`) |

Нет хэндшейка, нет sequence number, нет подтверждений доставки.
Таргет идемпотентен для `ping` и `run` — при потере строки хост повторяет.

---

### Жизненный цикл сессии

```bash
Хост                                           Таргет
 │                                                │
 │    [USB SDP: прошивка загружена]               │
 │    [CDC ACM: порт открыт]                      │
 │◄─── {"type":"session_start","fw":"0.1.0",...}  │  автоматически
 │                                                │
 │──── {"type":"cmd","cmd":"ping"}  ─────────────►│
 │◄─── {"type":"pong"}                            │
 │                                                │
 │──── {"type":"cmd","cmd":"run","id":"sdram"} ──►│
 │◄─── {"type":"test_begin","id":"sdram",...}     │
 │◄─── {"type":"test_result","id":"sdram",...}    │
 │                                                │
 │──── {"type":"cmd","cmd":"run_all"}  ───────────►│
 │◄─── {"type":"test_begin","id":"sdram",...}     │
 │◄─── {"type":"test_result",...}                 │
 │     … по одному для каждого теста …            │
 │◄─── {"type":"summary","overall":"pass",...}    │
```

`session_start` отправляется **автоматически** при каждом старте, до получения
первой команды. Хост должен быть готов принять его сразу после открытия порта.

---

### Команды хоста → таргет

Все команды имеют `"type":"cmd"`. Поле `"cmd"` определяет действие.

#### `ping` — проверка связи

```json
→ {"type":"cmd","cmd":"ping"}
← {"type":"pong"}
```

#### `run` — запуск одного теста

```json
→ {"type":"cmd","cmd":"run","id":"sdram"}
← {"type":"test_begin","id":"sdram","name":"SDRAM 32 MB","critical":true}
← {"type":"test_result","id":"sdram","status":"pass","ms":312,"detail":""}
```

Если `id` не найден:

```json
← {"ok":false,"error":"UNKNOWN_TEST"}
```

#### `run_all` — запуск всех тестов по реестру

```json
→ {"type":"cmd","cmd":"run_all"}
← {"type":"test_begin","id":"sdram",...}
← {"type":"test_result","id":"sdram","status":"pass","ms":312,"detail":""}
← {"type":"test_begin","id":"qspi",...}
← {"type":"test_result","id":"qspi","status":"pass","ms":88,"detail":""}
← ... (остальные тесты) ...
← {"type":"summary","passed":7,"failed":0,"skipped":1,"overall":"pass"}
```

#### `confirm` — ответ оператора на интерактивный шаг

```json
→ {"type":"confirm","id":"display_red","confirmed":true}
```

Поле `id` должно совпадать с `id` из `confirm_request`.
Ответ после истечения `timeout_ms` игнорируется — таргет уже перешёл в SKIP.

---

### События таргета → хост

#### `session_start`

```json
{
  "type":      "session_start",
  "fw":        "0.1.0",
  "target":    "IMXRT1052",
  "uptime_ms": 0
}
```

#### `test_begin`

```json
{
  "type":     "test_begin",
  "id":       "sdram",
  "name":     "SDRAM 32 MB",
  "critical": true
}
```

#### `test_result`

```json
{
  "type":   "test_result",
  "id":     "sdram",
  "status": "pass",
  "ms":     312,
  "detail": ""
}
```

| `status` | Смысл |
|---|---|
| `"pass"` | Тест пройден |
| `"fail"` | Тест провален; `detail` содержит описание (до 95 символов) |
| `"skip"` | Пропущен: нет оборудования, таймаут, отказ оператора, critical fail выше |

Примеры `detail`: `"addr=0x80001000 expected=0xA5 got=0x00"`, `"JEDEC ID mismatch"`.

#### `confirm_request`

```json
{
  "type":       "confirm_request",
  "id":         "display_red",
  "prompt":     "Экран залит красным цветом?",
  "timeout_ms": 15000
}
```

Хост отображает `prompt` оператору. Авторитетный таймаут — на таргете.
Хост может дублировать countdown для UX.

#### `summary`

```json
{
  "type":    "summary",
  "passed":  6,
  "failed":  1,
  "skipped": 0,
  "overall": "fail"
}
```

`"overall":"fail"` — если хотя бы один `critical` тест провален.
`"overall":"pass"` — все `critical` тесты прошли (non-critical могут fail).

---

### Ошибки протокола

```json
← {"ok":false,"error":"PARSE_ERR"}      — строка не распознана как JSON-lines
← {"ok":false,"error":"UNKNOWN_CMD"}    — неизвестный "cmd" или "type"
← {"ok":false,"error":"UNKNOWN_TEST"}   — "id" не найден в реестре
← {"ok":false,"error":"LINE_TOO_LONG"}  — строка превысила 128 байт
← {"ok":false,"error":"BUSY"}           — таргет выполняет тест
```

---

### Интерактивные тесты

#### uSD — вставить карту

```bash
← {"type":"confirm_request","id":"usd_insert","prompt":"Вставьте microSD","timeout_ms":30000}
→ {"type":"confirm","id":"usd_insert","confirmed":true}
← {"type":"test_begin","id":"usd",...}
← {"type":"test_result","id":"usd","status":"pass","ms":541,"detail":""}
```

Если оператор отказался или таймаут:

```bash
← {"type":"test_result","id":"usd","status":"skip","ms":0,"detail":"operator skipped"}
```

#### Display RGB888 — подтвердить цвета

Четыре шага R/G/B/W. Итог — AND всех подтверждений.

```bash
← {"type":"test_begin","id":"display",...}
← {"type":"confirm_request","id":"display_red","prompt":"Экран красный?","timeout_ms":15000}
→ {"type":"confirm","id":"display_red","confirmed":true}
← {"type":"confirm_request","id":"display_green",...}
→ {"type":"confirm","id":"display_green","confirmed":true}
← {"type":"confirm_request","id":"display_blue",...}
→ {"type":"confirm","id":"display_blue","confirmed":true}
← {"type":"confirm_request","id":"display_white",...}
→ {"type":"confirm","id":"display_white","confirmed":false}
← {"type":"test_result","id":"display","status":"fail","ms":22103,
   "detail":"display_white not confirmed"}
```

#### Кнопки — нажать физически

**Особый случай:** `confirm_request` используется как инструкция оператору,
но хост **не отправляет `confirm`**. Таргет сам детектирует нажатие через
`bsp_button` и переходит к следующему событию.

```bash
← {"type":"test_begin","id":"buttons",...}
← {"type":"confirm_request","id":"btn1_press","prompt":"Нажмите Test_But_1","timeout_ms":10000}
  [таргет ждёт bsp_button — без JSON confirm от хоста]
← {"type":"confirm_request","id":"btn2_press","prompt":"Нажмите Test_But_2","timeout_ms":10000}
← {"type":"test_result","id":"buttons","status":"pass","ms":3821,"detail":""}
```

---

## Матрица тестов

| ID | Название | Тип | Critical | M5 HIL | Confirm |
|---|---|---|---|---|---|
| `sdram` | SDRAM 32 MB | self | ✅ | ❌ | ❌ |
| `qspi` | QSPI Flash 8 MB | self | ✅ | ❌ | ❌ |
| `usd` | uSD SDIO | self+interactive | ❌ | ❌ | ✅ pre_confirm |
| `display` | Display RGB888 | interactive | ❌ | ❌ | ✅ 4×в run() |
| `buttons` | Test_But_1/2 | interactive | ❌ | ❌ | prompt only |
| `can` | CAN loopback | HIL | ❌ | ✅ | ❌ |
| `uart_ttl` | UART TTL | HIL | ❌ | ✅ | ❌ |
| `uart_iso` | UART ISO / RS_RX | HIL | ❌ | ✅ | ❌ |
| `opto` | Opto EXT_IN1/IN2 | HIL | ❌ | ✅ | ❌ |

**Типы confirm:**

- **pre_confirm** — test_runner отправляет `confirm_request` до вызова `run()`,
  ждёт JSON-ответ через state machine (асинхронно).
- **в run()** — тест сам вызывает `test_runner_wait_confirm()` изнутри `run()`,
  блокируется до ответа (синхронно).
- **prompt only** — `protocol_send_confirm_request()` отправляется как UI-подсказка,
  хост не отвечает JSON, таргет ждёт физического события.

---

## Как добавить новый тест

### Шаг 1 — Создать файл теста

```c
/* firmware/test/src/tests/test_foo.c */

#include "test_module.h"

#include "bsp/foo.h"        /* BSP модуль тестируемой периферии */
#include "bsp/usb_cdc.h"    /* bsp_usb_cdc_poll() для длинных тестов */

#include <stdint.h>
#include <stdio.h>

static test_result_t test_foo_run(void)
{
    test_result_t result = { .status = TEST_STATUS_PASS, .duration_ms = 0U };
    result.detail[0] = '\0';

    /* Длинные операции должны периодически звать bsp_usb_cdc_poll(),
     * чтобы USB-стек оставался живым пока run() блокирует главный цикл. */

    bsp_foo_status_t status = bsp_foo_test();

    if (status != BSP_OK)
    {
        result.status = TEST_STATUS_FAIL;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE,
                        "bsp_foo_test returned %d", (int) status);
    }

    return result;
}

const test_module_t k_test_foo = {
    .id                 = "foo",          /* короткий ASCII-ключ         */
    .name               = "Foo Peripheral",
    .critical           = false,          /* true → run_all стопится при fail */
    .requires_hil       = false,          /* true → нужен M5StampPLC     */
    .pre_confirm_prompt = NULL,           /* строка → pre_confirm механизм */
    .init               = NULL,           /* bsp_foo_init если нужен     */
    .run                = test_foo_run,
    .deinit             = NULL,
};
```

### Шаг 2 — Зарегистрировать в реестре

**Файл:** `firmware/test/src/test_runner.c`

```c
/* Forward declarations */
extern const test_module_t k_test_sdram;
extern const test_module_t k_test_foo;    /* ← добавить */

static const test_module_t *const k_registry[] = {
    &k_test_sdram,
    &k_test_foo,    /* ← добавить */
};
```

### Шаг 3 — Добавить в CMakeLists.txt

**Файл:** `firmware/test/CMakeLists.txt`

```cmake
add_executable(
  ${TARGET_NAME}
  src/main.c
  src/cli.c
  src/protocol.c
  src/test_runner.c
  src/tests/test_sdram.c
  src/tests/test_foo.c    # ← добавить
  ...
)

target_link_libraries(
  ${TARGET_NAME} PRIVATE
  ...
  bsp_foo     # ← добавить BSP модуль
)
```

### Шаг 4 — Обновить матрицу тестов

Добавить строку в таблицу в этом README.

### Шаблоны для разных типов тестов

#### Self-тест с инициализацией

```c
static void test_foo_init(void)
{
    bsp_foo_init();
}

static void test_foo_deinit(void)
{
    bsp_foo_deinit();
}

const test_module_t k_test_foo = {
    .id     = "foo",
    .init   = test_foo_init,
    .run    = test_foo_run,
    .deinit = test_foo_deinit,
    ...
};
```

#### Интерактивный тест (confirm внутри run)

```c
#include "test_runner.h"    /* test_runner_wait_confirm() */
#include "protocol.h"       /* protocol_send_confirm_request() */

static test_result_t test_foo_run(void)
{
    test_result_t result = { .status = TEST_STATUS_PASS };

    confirm_params_t step = {
        .id         = "foo_step1",
        .prompt     = "Выполните действие и подтвердите",
        .timeout_ms = 15000U,
    };

    if (!test_runner_wait_confirm(&step))
    {
        /* таймаут или отказ */
        result.status = TEST_STATUS_SKIP;
        (void) snprintf(result.detail, TEST_DETAIL_SIZE, "%s", "foo_step1 not confirmed");
        return result;
    }

    /* продолжаем тест */
    return result;
}
```

#### Тест с pre_confirm (вставить карту, подключить кабель)

```c
const test_module_t k_test_foo = {
    .id                 = "foo",
    .pre_confirm_prompt = "Подключите кабель к разъёму X и нажмите OK",
    .run                = test_foo_run,
    ...
};
/* test_runner сам отправит confirm_request перед вызовом run() */
```

---

## Host unit-тесты

Фреймворк модулей firmware_test покрыт host unit-тестами (Unity + fff).
Тесты компилируются clang-17 на хосте без ARM-специфики.

### Что покрыто

| Таргет | Что тестирует | Тест-файл |
|---|---|---|
| `test_protocol` | сериализация JSON (все event types) | `tests/host/protocol/test_protocol.c` |
| `test_cli` | парсинг входящих строк, диспатч по type | `tests/host/cli/test_cli.c` |
| `test_firmware_runner` | state machine (IDLE/PRE_CONFIRM/RUNNING), реестр | `tests/host/runner/test_firmware_runner.c` |

### Запуск

```bash
# Все host-тесты
just build::test-host

# Только один тест (вербозный вывод Unity)
ctest --preset host-debug-test -R test_cli -V

# Напрямую
./build/host-debug/tests/host/test_cli
```

### Добавление host-теста для нового тест-модуля

Host unit-тесты для `test_sdram.c` и подобных — опциональны. Тест-модули
проверяются через HIL pytest (`tools/hil/`). Если в тест-модуле есть
нетривиальная логика (парсинг результатов, конечный автомат, retry) —
стоит добавить host-тест.

Гайд: `docs/testing/host/HOST_CREATE_TEST.md`.

### Моки и UNIT_TEST seam

`test_runner.c` компилируется с `-DUNIT_TEST` — это открывает seam для
подстановки тестового реестра без изменения production-кода:

```c
/* В тест-файле предоставляем свои модули */
const test_module_t *g_unit_test_registry[8];
size_t               g_unit_test_registry_size = 0U;

static void set_registry(const test_module_t **pp_mods, size_t count) { ... }

void test_run_all_critical_fail_skips_remaining(void)
{
    const test_module_t *mods[] = { &K_MOD_PASS, &K_MOD_CRIT_FAIL, &K_MOD_PASS };
    set_registry(mods, 3U);
    test_runner_run_all();
    /* assertions... */
}
```

Стандартный список моков для каждого теста:

| Зависимость | fff fake |
|---|---|
| `cli_send()` | `FAKE_VOID_FUNC(cli_send, const char *)` + custom_fake с копией |
| `bsp_tick_get_ms()` | `FAKE_VALUE_FUNC(uint32_t, bsp_tick_get_ms)` |
| `bsp_usb_cdc_poll()` | `FAKE_VOID_FUNC(bsp_usb_cdc_poll)` |
| `cli_process()` | `FAKE_VOID_FUNC(cli_process)` |
| `protocol_send_test_result()` | custom_fake — копируем `*p_result` по значению |

> **Ловушка dangling pointer:** `protocol_send_test_result` получает указатель
> на стековую переменную внутри `execute_test()`. После возврата указатель
> инвалиден — используй `custom_fake` с `s_captured = *p_result` пока стек жив.

---

## Версионирование

`FIRMWARE_TEST_VERSION` в `protocol.h` — единственная точка правды о версии.
Поле `"fw"` в `session_start` несёт эту строку.

При несовместимых изменениях протокола (новое обязательное поле, изменение
семантики) — bumping версии + обновление этого документа.

Хост должен сверять `"fw"` при подключении и предупреждать оператора при
несовпадении ожидаемой версии.

---

## Архитектурные решения (закрыты)

> Не пересматривать без явного запроса.

| Решение | Обоснование |
|---|---|
| Единственный транспорт — USB CDC ACM | HIL ELF-прошивки используют отдельный канал (UART + bsp_uart_host) |
| Парсинг JSON — strstr без cJSON | Схема фиксирована, cJSON избыточен |
| SDRAM тест — через firmware_test, не HIL ELF | Тест идёт командами по USB CDC |
| IR и RTC — не реализуются | Вне scope рекламационной диагностики |
| Тесты атомарны | Инженер сам решает что проверять |
| Тест-логика на таргете | Хост — тонкий клиент, нет дублирования логики |

# firmware_test — Протокол диагностики v2

> **Расположение в репозитории:** `docs/testing/PROTOCOL.md`
>
> Документ описывает протокол обмена между диагностической прошивкой
> (`firmware_test`) и хостовым ПО сервисного инженера.
> Актуален для: `firmware_test v0.1.0+`, `protocol.h v2`.

---

## Назначение и контекст

`firmware_test` — специализированная прошивка для диагностики плат
**MIMXRT1052CVJ5B**, вернувшихся по рекламации. Запускается сервисным
инженером через USB CDC ACM (разъём J2). Загружается через BootROM (USB SDP)
без предварительной прошивки загрузчика.

**Стенд:**

```bash
[Хост-ПК сервисного инженера]
        │  USB CDC ACM (J2)
        │  JSON-lines, 1 строка = 1 сообщение
        ▼
[Плата MIMXRT1052 с firmware_test]
        │  GPIO / LPUART / SEMC / FlexSPI / USDHC
        ▼
[Периферия: SDRAM, QSPI Flash, uSD, Display, CAN, UART, Opto]
        ▲
[M5StampPLC — управление внешними сигналами для HIL тестов]
```

**Принцип работы:**

- Вся тест-логика живёт **на таргете** (`test_runner.c`, `tests/*.c`).
- Хост — тонкий клиент: отправляет команды, отображает события, управляет
  интерактивными шагами.
- Инженер запускает тесты **атомарно** (один тест за раз) или все подряд
  (`run_all`). Последовательность не фиксирована — инженер сам решает,
  что проверять.

---

## Транспорт

| Параметр                  | Значение                                                     |
| ------------------------- | ------------------------------------------------------------ |
| Интерфейс                 | USB CDC ACM                                                  |
| Разъём                    | J2                                                           |
| Кодировка                 | UTF-8                                                        |
| Фреймирование             | JSON-lines: каждое сообщение — одна строка, завершается `\n` |
| Максимальная длина строки | 128 байт (включая `\n`)                                      |
| CR+LF                     | Принимается (таргет отбрасывает `\r` перед `\n`)             |
| Направление               | Двунаправленный, half-duplex по логике                       |

Нет хэндшейка, нет sequence number, нет подтверждений доставки. При потере
строки хост повторяет команду — таргет идемпотентен для `ping` и `run`.

---

## Формат сообщений

Все сообщения — JSON-объекты в одну строку (`\n` в конце).

### Ключевые поля

Каждое сообщение содержит поле `"type"`, определяющее его смысл:

```
Хост → Таргет:  "type": "cmd"      — команда
                "type": "confirm"   — ответ оператора на интерактивный шаг

Таргет → Хост:  "type": "session_start"    — таргет готов
                "type": "pong"             — ответ на ping
                "type": "test_begin"       — тест стартовал
                "type": "test_result"      — тест завершён
                "type": "confirm_request"  — ожидание действия оператора
                "type": "summary"          — итог run_all
                "ok": false, "error": "…"  — ошибка протокола
```

---

## Жизненный цикл сессии

```bash
Хост                                        Таргет
 │                                              │
 │    [прошивка загружена через USB SDP]        │
 │    [USB CDC установлен]                      │
 │◄─── {"type":"session_start","fw":"0.1.0",   │
 │       "target":"IMXRT1052","uptime_ms":0}    │
 │                                              │
 │──── {"type":"cmd","cmd":"ping"}  ──────────►│
 │◄─── {"type":"pong"}  ──────────────────────  │
 │                                              │
 │   [инженер выбирает тест]                    │
 │                                              │
 │──── {"type":"cmd","cmd":"run","id":"sdram"} ►│
 │◄─── {"type":"test_begin","id":"sdram",...}   │
 │◄─── {"type":"test_result","id":"sdram",...}  │
 │                                              │
 │──── {"type":"cmd","cmd":"run_all"}  ────────►│
 │◄─── {"type":"test_begin","id":"sdram",...}   │
 │◄─── {"type":"test_result","id":"sdram",...}  │
 │     … (каждый тест в реестре) …              │
 │◄─── {"type":"summary","overall":"pass",...}  │
 │                                              │
```

`session_start` отправляется **автоматически** при каждом старте таргета,
до получения первой команды. Хост должен быть готов принять его сразу после
открытия CDC порта.

---

## Команды хоста → таргет (`"type":"cmd"`)

### `ping`

Проверка связи. Таргет отвечает немедленно.

```json
→ {"type":"cmd","cmd":"ping"}
← {"type":"pong"}
```

---

### `run` — запуск одного теста

Запустить тест по идентификатору. Если тест требует предварительного
подтверждения оператора (`pre_confirm_prompt` задан), таргет сначала пошлёт
`confirm_request`.

```json
→ {"type":"cmd","cmd":"run","id":"sdram"}
← {"type":"test_begin","id":"sdram","name":"SDRAM 32 MB","critical":true}
← {"type":"test_result","id":"sdram","status":"pass","ms":312,"detail":""}
```

Если `id` не найден в реестре:

```json
← {"ok":false,"error":"UNKNOWN_TEST"}
```

---

### `run_all` — запуск всех тестов по реестру

Запускает все тест-модули в порядке реестра. Если тест помечен `"critical":true`
и вернул `"status":"fail"` — выполнение прерывается, остальные тесты
получают `"status":"skip"` в итоге (но `summary` всё равно отправляется).

```json
→ {"type":"cmd","cmd":"run_all"}
← {"type":"test_begin","id":"sdram","name":"SDRAM 32 MB","critical":true}
← {"type":"test_result","id":"sdram","status":"pass","ms":312,"detail":""}
← {"type":"test_begin","id":"qspi","name":"QSPI Flash 8 MB","critical":true}
← {"type":"test_result","id":"qspi","status":"pass","ms":88,"detail":""}
← ... (остальные тесты) ...
← {"type":"summary","passed":7,"failed":0,"skipped":1,"overall":"pass"}
```

---

## События таргета → хост

### `session_start`

Таргет готов к работе. Отправляется автоматически при старте.

```json
{
  "type":       "session_start",
  "fw":         "0.1.0",
  "target":     "IMXRT1052",
  "uptime_ms":  0
}
```

| Поле        | Тип    | Описание                   |
| ----------- | ------ | -------------------------- |
| `fw`        | string | Версия firmware_test       |
| `target`    | string | Идентификатор платформы    |
| `uptime_ms` | number | Время с момента старта, мс |

---

### `test_begin`

Тест начат. Отправляется непосредственно перед вызовом `run()`.

```json
{
  "type":     "test_begin",
  "id":       "sdram",
  "name":     "SDRAM 32 MB",
  "critical": true
}
```

---

### `test_result`

Тест завершён (pass / fail / skip).

```json
{
  "type":   "test_result",
  "id":     "sdram",
  "status": "pass",
  "ms":     312,
  "detail": ""
}
```

| `status` | Смысл                                                                        |
| -------- | ---------------------------------------------------------------------------- |
| `"pass"` | Тест пройден                                                                 |
| `"fail"` | Тест провален; поле `detail` содержит описание                               |
| `"skip"` | Тест пропущен (нет оборудования, таймаут оператора, прерван `critical` fail) |

Поле `detail` — произвольная ASCII-строка до 95 символов. При `pass` — пустая.
Примеры: `"addr=0x80001000 expected=0xA5 got=0x00"`, `"JEDEC ID mismatch"`.

---

### `confirm_request`

Таргет ожидает действия оператора. Используется интерактивными тестами:
display (подтвердить цвет), кнопки (нажать кнопку), uSD (вставить карту).

```json
{
  "type":       "confirm_request",
  "id":         "display_red",
  "prompt":     "Экран залит красным цветом?",
  "timeout_ms": 15000
}
```

Хост должен отобразить `prompt` оператору и ждать его реакции. Если оператор
не ответил за `timeout_ms` — таргет переходит в `SKIP` для этого шага
автоматически. Хост может дублировать таймаут на своей стороне для UX, но
авторитетный таймаут — на таргете.

---

### `summary`

Итог `run_all`. Отправляется после завершения последнего теста в реестре или
после прерывания по critical fail.

```json
{
  "type":    "summary",
  "passed":  6,
  "failed":  1,
  "skipped": 0,
  "overall": "fail"
}
```

`"overall": "fail"` если хотя бы один `critical` тест провален.
`"overall": "pass"` если все `critical` тесты прошли (non-critical могут fail).

---

### `confirm` (хост → таргет)

Ответ оператора на `confirm_request`. Поле `"id"` должно совпадать с `id`
из `confirm_request`.

```json
→ {"type":"confirm","id":"display_red","confirmed":true}
```

Если `"confirmed": false` — таргет записывает `TEST_STATUS_FAIL` для этого шага.
Если ответ пришёл после истечения `timeout_ms` — таргет игнорирует его
(уже перешёл в SKIP).

---

### Ошибки протокола

```json
← {"ok":false,"error":"PARSE_ERR"}
← {"ok":false,"error":"UNKNOWN_CMD"}
← {"ok":false,"error":"UNKNOWN_TEST"}
← {"ok":false,"error":"LINE_TOO_LONG"}
← {"ok":false,"error":"BUSY"}
```

| Код             | Причина                                         |
| --------------- | ----------------------------------------------- |
| `PARSE_ERR`     | Строка не является валидным JSON-lines запросом |
| `UNKNOWN_CMD`   | Поле `"cmd"` содержит неизвестное значение      |
| `UNKNOWN_TEST`  | Поле `"id"` в `run` не найдено в реестре        |
| `LINE_TOO_LONG` | Входящая строка превысила 128 байт              |
| `BUSY`          | Таргет выполняет тест, новая команда отклонена  |

---

## Матрица тестов

| ID         | Название              | Тип                | Critical | HIL (M5) | Интерактивный      |
| ---------- | --------------------- | ------------------ | -------- | -------- | ------------------ |
| `sdram`    | SDRAM 32 MB           | self               | ✅        | ❌        | ❌                  |
| `qspi`     | QSPI Flash 8 MB       | self               | ✅        | ❌        | ❌                  |
| `usd`      | uSD (SDIO)            | self + interactive | ❌        | ❌        | ✅ (вставить карту) |
| `display`  | Display RGB888        | interactive        | ❌        | ❌        | ✅ (цвета R/G/B/W)  |
| `buttons`  | Кнопки Test_But_1/2   | interactive        | ❌        | ❌        | ✅ (нажать кнопки)  |
| `can`      | CAN                   | HIL                | ❌        | ✅        | ❌                  |
| `uart_ttl` | UART TTL              | HIL                | ❌        | ✅        | ❌                  |
| `uart_iso` | UART ISO / RS_RX Opto | HIL                | ❌        | ✅        | ❌                  |
| `opto`     | Opto-in EXT_IN1/IN2   | HIL                | ❌        | ✅        | ❌                  |

**Типы тестов:**

- **self** — таргет тестирует периферию самостоятельно, без внешних сигналов.
- **interactive** — требует действия оператора через механизм `confirm_request`.
- **HIL** — требует M5StampPLC для генерации внешних сигналов
  (реле, CAN фреймы, UART echo).

---

## Интерактивные тесты — детальный поток

### uSD

Карта вставляется оператором по запросу. Тест не входит в критический путь.
Pre-confirm обрабатывается `test_runner` до вызова `run()`.

```bash
← {"type":"confirm_request","id":"usd","prompt":"Insert microSD card and press OK","timeout_ms":30000}
→ {"type":"confirm","id":"usd","confirmed":true}
← {"type":"test_begin","id":"usd","name":"microSD (SDIO)","critical":false}
← {"type":"progress","test":"usd","step":"card_detect","status":"ok"}
← {"type":"progress","test":"usd","step":"mount","status":"ok"}
← {"type":"progress","test":"usd","step":"write","status":"ok"}
← {"type":"progress","test":"usd","step":"read_compare","status":"ok"}
← {"type":"test_result","id":"usd","status":"pass","ms":741,"detail":""}
```

Если оператор отказался (`"confirmed":false`):

```bash
← {"type":"test_begin","id":"usd","name":"microSD (SDIO)","critical":false}
← {"type":"test_result","id":"usd","status":"skip","ms":0,"detail":"operator declined"}
```

Если истёк таймаут (30 с без ответа):

```bash
← {"type":"test_begin","id":"usd","name":"microSD (SDIO)","critical":false}
← {"type":"test_result","id":"usd","status":"skip","ms":0,"detail":"confirm timeout"}
```

---

### Display (RGB888)

Четыре шага: красный, зелёный, синий, белый. Итог — AND всех подтверждений.
Одновременно верифицируется подсветка (PWM включён).

```bash
← {"type":"test_begin","id":"display",...}
← {"type":"confirm_request","id":"display_red","prompt":"Экран залит красным?","timeout_ms":15000}
→ {"type":"confirm","id":"display_red","confirmed":true}
← {"type":"confirm_request","id":"display_green","prompt":"Экран залит зелёным?","timeout_ms":15000}
→ {"type":"confirm","id":"display_green","confirmed":true}
← {"type":"confirm_request","id":"display_blue","prompt":"Экран залит синим?","timeout_ms":15000}
→ {"type":"confirm","id":"display_blue","confirmed":true}
← {"type":"confirm_request","id":"display_white","prompt":"Экран залит белым?","timeout_ms":15000}
→ {"type":"confirm","id":"display_white","confirmed":false}
← {"type":"test_result","id":"display","status":"fail","ms":22103,"detail":"display_white not confirmed"}
```

---

### Кнопки (Test_But_1 / Test_But_2)

Два шага. Таргет ждёт физического нажатия через `bsp_button`, не через confirm.
`confirm_request` здесь используется как инструкция оператору — ответом является
не JSON, а сам факт нажатия кнопки, который таргет детектирует самостоятельно.

```bash
← {"type":"test_begin","id":"buttons",...}
← {"type":"confirm_request","id":"btn1_press","prompt":"Нажмите кнопку Test_But_1","timeout_ms":10000}
  [таргет ждёт bsp_button_get(BTN_TEST_1) == PRESSED, таймаут 10 с]
← {"type":"confirm_request","id":"btn2_press","prompt":"Нажмите кнопку Test_But_2","timeout_ms":10000}
  [таргет ждёт bsp_button_get(BTN_TEST_2) == PRESSED, таймаут 10 с]
← {"type":"test_result","id":"buttons","status":"pass","ms":3821,"detail":""}
```

> **Важно:** для теста кнопок таргет не ждёт `{"type":"confirm",…}` от хоста.
> Хост отображает `prompt` оператору и ждёт следующего события от таргета.
> Нажатие детектируется прошивкой через `bsp_button`, не через CDC.

---

## Рекомендации для разработчика хостового ПО

### Открытие порта

```bash
1. Найти CDC ACM устройство (VID/PID NXP или зарегистрированный).
2. Открыть порт (любой baud rate — CDC игнорирует его).
3. Ждать строку с "type":"session_start" — таймаут 10 с.
4. Если не получен — переоткрыть порт или перезагрузить таргет.
```

### Чтение событий

```bash
- Читать побайтово или буфером, буферизировать до '\n'.
- Одна строка = одно JSON-сообщение.
- Неизвестное поле "type" — игнорировать (forward-compatibility).
- Парсить минимально: поле "type" определяет дальнейший разбор.
```

### Отправка команд

```bash
- Завершать каждую строку '\n' (не '\r\n').
- Не отправлять следующую команду до получения финального события
  предыдущей (test_result или error).
- Исключение: "ping" можно отправлять в любой момент, если таргет не BUSY.
```

### Обработка confirm_request

```bash
1. Получить "confirm_request" → отобразить "prompt" оператору.
2. Дождаться реакции оператора (кнопка в UI, клавиша в TUI).
3. Исключение — тест "buttons": не отправлять confirm, просто ждать
   следующего события от таргета.
4. Для всех остальных тестов — отправить:
   {"type":"confirm","id":"<тот же id>","confirmed":true/false}
5. Хост может показывать countdown по timeout_ms для UX,
   но не обязан — таргет сам завершит по таймауту.
```

---

## Реализация на стороне таргета

### Модули прошивки

```bash
firmware/test/src/
├── main.c           — инициализация, главный цикл, вызов cli_process()
├── cli.h / cli.c    — IO-слой: буферизация строк, диспатч по "type"
├── protocol.h / .c  — сериализация исходящих событий через cli_send()
├── test_module.h    — интерфейс тест-модуля (структура test_module_t)
├── test_runner.h/.c — реестр тестов, state machine, confirm механизм
└── tests/
    ├── test_sdram.c
    ├── test_qspi.c
    ├── test_usd.c
    ├── test_display.c
    ├── test_buttons.c
    ├── test_can.c
    ├── test_uart_ttl.c
    ├── test_uart_iso.c
    └── test_opto.c
```

### State machine test_runner

```
        ┌─────────────────────────────────────────┐
        │                  IDLE                   │◄──────────────────┐
        │  Ждём команду от хоста                  │                   │
        └───────────────┬─────────────────────────┘                   │
                        │ cmd: run / run_all                          │
                        ▼                                             │
        ┌─────────────────────────────────────────┐                   │
        │              PRE_CONFIRM                │                   │
        │  pre_confirm_prompt != NULL?             │                   │
        │  → protocol_send_confirm_request()      │                   │
        └───────────────┬─────────────────────────┘                   │
                        │ confirm получен / NULL                      │
                        ▼                                             │
        ┌─────────────────────────────────────────┐                   │
        │               RUNNING                   │                   │
        │  protocol_send_test_begin()              │                   │
        │  mod->init() если задан                  │                   │
        │  result = mod->run()                     │                   │
        │  mod->deinit() если задан                │                   │
        │  protocol_send_test_result()             │                   │
        └───────────────┬─────────────────────────┘                   │
                        │                                             │
                        ├── run_all: следующий тест ──────────────────┤
                        │                                             │
                        └── run_all завершён: protocol_send_summary() ┘
                            run: сразу → IDLE
```

### Добавление нового теста

1. Создать `firmware/test/src/tests/test_foo.c` с реализацией `test_result_t test_foo_run(void)`.
2. Объявить дескриптор:
   ```c
   const test_module_t k_test_foo = {
       .id                = "foo",
       .name              = "Foo Peripheral",
       .critical          = false,
       .requires_hil      = false,
       .pre_confirm_prompt = NULL,
       .init              = NULL,
       .run               = test_foo_run,
       .deinit            = NULL,
   };
   ```
3. Добавить `&k_test_foo` в реестр `test_runner.c` — одна строка.
4. Добавить `tests/test_foo.c` в `CMakeLists.txt` таргета.

---

## Версионирование протокола

Поле `"fw"` в `session_start` — версия прошивки, а не версия протокола.
При несовместимых изменениях протокола (новое обязательное поле, изменение
семантики существующего) — bumping `FIRMWARE_TEST_VERSION` в `protocol.h`
с соответствующим обновлением этого документа.

Хост должен проверять `"fw"` и предупреждать оператора при несовпадении
ожидаемой версии.

# firmware_test — Architecture

> Target: NXP IMXRT1052CVJ5B  
> Версия документа: 0.1  
> Статус: draft

---

## 1. Назначение

`firmware_test` — входная тестовая прошивка для проверки работоспособности платы при производстве и во время разработки. Запускается напрямую через BootROM (USB Serial Download), без предварительной прошивки загрузчика. После успешного прохождения всех тестов инициирует фазу провижининга (привязка Chip UID к версиям ПО).

---

## 2. Workflow прошивки платы

```bash
BootROM (USB Serial Download, встроен в IMXRT1052)
    ↓
firmware_test (залит напрямую)
    ↓  [тесты прошли, provisioning выполнен]
Флашим: Bootloader + App
    ↓
Ждём heartbeat Bootloader → App
    ↓
Плата принята
```

Вариант с предварительной заливкой загрузчика не используется — BootROM является надёжным и всегда доступным recovery-path, не зависящим от состояния Flash.

---

## 3. Высокоуровневая архитектура

```bash
┌─────────────────────────────────────────────────────┐
│                  firmware_test                      │
│                                                     │
│  USB CDC ──► Protocol ──► Test runner ──► Local UI  │
│              (JSON-lines)  (sequencer)   (LED+disp) │
│                                                     │
│  ┌─────────────────────┐  ┌──────────────────────┐  │
│  │     self-tests      │  │  HIL tests           │  │
│  │  SDRAM  QSPI  uSD   │  │  CAN  UART  Opto-in  │  │
│  │  RTC    Display  IR │  │  UART ISO  IR burst  │  │
│  └─────────────────────┘  └──────────────────────┘  │
│                                                     │
│                    HAL / BSP                        │
└─────────────────────────────────────────────────────┘
```

---

## 4. Компоненты

### 4.1 USB CDC

Единственный канал связи с внешним миром. Представляется хосту как виртуальный COM-порт. Инициализируется первым, до запуска тестов. При старте ожидает подключения хоста с таймаутом — если хост не подключился, тесты запускаются автономно.

### 4.2 Protocol

Протокол — **JSON-lines**: каждое сообщение является отдельным JSON-объектом, завершённым символом `\n`. Библиотека: cJSON из NXP SDK.

Направление **хост → плата** (команды):

```json
{"type":"cmd","cmd":"run_all"}
{"type":"cmd","cmd":"run","id":"sdram"}
{"type":"confirm","id":"display","confirmed":true}
```

Направление **плата → хост** (события):

```json
{"type":"session_start","fw":"0.1.0","target":"IMXRT1052","uptime_ms":0}
{"type":"test_begin","id":"sdram","name":"SDRAM 32MB","critical":true}
{"type":"test_result","id":"sdram","status":"pass","ms":312,"detail":"32MB R/W OK"}
{"type":"confirm_request","id":"display","timeout_ms":15000}
{"type":"abort","reason":"critical_fail","id":"usd"}
{"type":"summary","passed":7,"failed":0,"skipped":2,"aborted":false,"overall":"pass"}
{"type":"provision_ready","chip_uid":"A3F2C1B400E70012"}
{"type":"provision_ack","fw":"1.0.0","bootloader":"1.0.0","recorded":true}
```

> **Примечание:** `uptime_ms` вместо Unix timestamp — RTC может быть не инициализирован на новой плате. Хост приклеивает реальное время самостоятельно.

### 4.3 Test runner

Центральный компонент. Хранит реестр тест-модулей, управляет порядком запуска, обрабатывает критические сбои, формирует `summary`.

**Порядок выполнения:**

1. Self-tests в порядке реестра
2. Проверка критических сбоев — если есть, HIL не запускается
3. HIL tests (только если Firefly подключён и self-tests прошли)
4. Summary report
5. Provisioning (только при `overall == pass`)

**Интерфейс тест-модуля (`test_module.h`):**

```cpp
typedef enum {
    TEST_STATUS_PASS = 0,
    TEST_STATUS_FAIL,
    TEST_STATUS_SKIP,
} test_status_t;

typedef struct {
    test_status_t status;
    uint32_t      duration_ms;
    char          detail[96];   /* диагностическая строка, опционально */
} test_result_t;

typedef struct {
    const char    *id;           /* "sdram", "qspi", "can" — ключ в JSON */
    const char    *name;         /* "SDRAM 32MB" — для display/лога */
    bool           critical;     /* abort HIL если FAIL */
    bool           requires_hil; /* пропустить если Firefly не готов */
    void          (*init)(void);
    test_result_t (*run)(void);
    void          (*deinit)(void);
} test_module_t;
```

### 4.4 Local UI

Отображает текущее состояние тестирования на светодиодах и дисплее. Получает события от Test runner. Дисплей при этом является частью тест-процесса (display_test).

**LED-паттерны:**

| Состояние              | LED1        | LED2        |
|------------------------|-------------|-------------|
| Тест выполняется       | мигает      | выкл        |
| Все тесты PASS         | вкл         | выкл        |
| Есть FAIL              | выкл        | вкл         |
| Ожидание подтверждения | оба мигают  |             |

---

## 5. Тест-модули

### 5.1 Self-tests

| ID         | Название         | Critical | Описание                                         |
|------------|------------------|----------|--------------------------------------------------|
| `sdram`    | SDRAM 32MB       | ✅        | Write/read паттерны по всему объёму              |
| `qspi`     | QSPI Flash       | ✅        | JEDEC ID + запись/чтение тестового сектора       |
| `usd`      | uSD (SDIO)       | ✅        | Mount + R/W тестового файла (SKIP если нет карты)|
| `rtc`      | RTC BM8563       | —        | I2C presence, set/get time                       |
| `display`  | Display RGB888   | —        | R/G/B/W заливки, подтверждение оператором        |
| `ir`       | IR receiver      | —        | GPIO idle state HIGH, peripheral init            |

**Display test — логика подтверждения:**

- Плата посылает `confirm_request` с `timeout_ms: 15000`
- Оператор нажимает **одну** кнопку: PASS или FAIL
- Если кнопка не нажата за 15 секунд — статус `SKIP` (ответственность на операторе)
- Одновременно проверяются обе кнопки — это полноценный тест кнопок

### 5.2 HIL tests (требуют Firefly AIO-3588Q)

| ID           | Название         | Стенд                        | Описание                               |
|--------------|------------------|------------------------------|----------------------------------------|
| `can`        | CAN              | Firefly CAN                  | Обмен фреймами, full-duplex            |
| `uart_ttl`   | UART TTL         | Firefly UART                 | Echo паттерн                           |
| `uart_iso`   | UART ISO +24V    | Firefly UART + интерф. плата | Только RX, Firefly посылает            |
| `opto`       | Opto-in +24V     | Firefly GPIO + интерф. плата | Все каналы, Firefly дёргает GPIO       |
| `ir_hil`     | IR burst         | Firefly GPIO + IR LED        | Приём burst 38 кГц, факт прерывания    |

---

## 6. Provisioning

Выполняется после `summary: overall == pass`. Не является тестом — это отдельный этап жизненного цикла платы.

**Источник UID:** регистры OCOTP (One-Time Programmable fuses), 64-bit Chip UID. Читается через HAL.

**Интерфейс (`provisioning.h`):**

```c
typedef struct {
    char chip_uid[17];          /* 64-bit UID как hex-строка, null-terminated */
    char fw_version[16];
    char bootloader_version[16];
    bool provisioned;
} provision_info_t;

/* вызывается только при overall == PASS */
void provisioning_run(provision_info_t *out);
```

**Поток:**

```bash
плата посылает provision_ready + chip_uid
    ↓
хост записывает в БД: uid ↔ fw_version ↔ bootloader_version
    ↓
хост посылает provision_ack
    ↓
плата устанавливает provisioned = true
```

Вся логика на стороне хоста (запись в БД, генерация сертификата, привязка партии). Прошивка только читает UID и ждёт подтверждения.

---

## 7. Реестр тестов

```c
/* test_registry.c */
static const test_module_t *tests[] = {
    &test_sdram,      /* critical */
    &test_qspi,       /* critical */
    &test_usd,        /* critical, SKIP если нет карты */
    &test_rtc,
    &test_display,    /* operator confirm */
    &test_ir,         /* self-test уровень */
    &test_can,        /* requires_hil */
    &test_uart_ttl,   /* requires_hil */
    &test_uart_iso,   /* requires_hil */
    &test_opto,       /* requires_hil */
    &test_ir_hil,     /* requires_hil, SKIP если нет IR на стенде */
};
```

---

## 8. Статусы тестов

| Статус | Значение                                              |
|--------|-------------------------------------------------------|
| `PASS` | Тест прошёл успешно                                   |
| `FAIL` | Тест провален, в `detail` диагностическая информация  |
| `SKIP` | Тест пропущен (нет карты, нет Firefly, таймаут оператора) |

---

## 9. Открытые вопросы

- [ ] Формат `detail` при FAIL для каждого теста (договориться между разработчиками)
- [ ] Handshake-протокол между firmware_test и Firefly (как плата узнаёт о готовности стенда)
- [ ] Полная схема интерфейсной платы для Firefly (оптовходы, IR LED, уровни +24V)
- [ ] GUI на сервере: формат отображения `summary` и хранение истории плат
# firmware_test — Plan of Development

> Версия: 0.4 | Обновлён после завершения Этапов 1–3 (bsp_sdram + bsp_qspi_flash).

---

## Контекст проекта

**Цель прошивки:** входной контроль платы MIMXRT1052CVJ5B на производстве.
Запускается через BootROM (USB SDP), без предварительной прошивки загрузчика.

**Стенд:**
- Хост подключается через USB CDC ACM — единственный канал firmware_test
- Тесты с внешними сигналами управляются через M5StampPLC
- Сервер запускает `tools/hil/` (разработка) или `tools/production/` (производство)

---

## Текущий статус

| Компонент                  | Статус | Примечание                             |
| -------------------------- | ------ | -------------------------------------- |
| `bsp_usb_cdc`              | ✅      | HIL тест пройден                       |
| firmware_test скелет       | ✅      | `main.c` + `cli.c`                     |
| Протокол v2 + test_runner  | ✅      | JSON-lines event-driven                |
| `bsp_sdram` + `test_sdram` | ✅      | 4 фазы: addr/data/seq/retention        |
| `bsp_qspi_flash`           | ✅      | W25Q64/128/256/512, ITCM, IRQ lock     |
| `test_qspi`                | ✅      | JEDEC + erase + rw + addr range        |
| `bsp_usd`                  | ✅      | bsp_sd + FatFS (firmware_test_fatfs)   |
| `test_usd`                 | ✅      | pre_confirm + 4 шага + progress events |
| Display test               | ⬜      | Этап 5                                 |
| Button test                | ⬜      | Этап 5                                 |
| CAN test                   | ⬜      | Этап 6 (bsp_can ✅)                     |
| UART TTL test              | ⬜      | Этап 6 (bsp_uart_host ✅)               |
| UART ISO test              | ⬜      | Этап 6                                 |
| Opto test                  | ⬜      | Этап 6 (bsp_opto ✅)                    |
| Provisioning               | ⬜      | Этап 7                                 |

---

## Закрытые архитектурные решения

> Не пересматривать без явного запроса.

- **Транспорт:** USB CDC ACM — единственный канал. UART не используется в firmware_test.
- **Парсинг JSON:** без cJSON, строковый `strstr`. Входящее поле всегда `"type"` / `"cmd"`.
- **SDRAM и DCD:** SEMC инициализируется DCD до `main()`. `bsp_sdram_init()` только верифицирует.
- **SDRAM тест:** прогоняется командами через firmware_test, не отдельным HIL ELF.
- **QSPI-функции в ITCM:** `AT_QUICKACCESS_SECTION_CODE` + `__STARTUP_INITIALIZE_RAMFUNCTION` в CMakeLists.
- **QSPI IRQ lock:** `__get_PRIMASK()` + DSB/ISB. Публичные API под полным lock.
- **W25Q256/512:** dedicated 4-byte opcodes, без Enter 4-Byte Mode (0xB7).
- **Порядок init в main.c:** `bsp_qspi_init()` до `bsp_tick_init()`.
- **IR и RTC:** не реализуются.
- **Производственный runner:** Вариант D — отдельный `tools/production/` без pytest.

---

## Этап 4 — `bsp_usd` + `test_usd`

### Аппаратный контекст

| Параметр         | Значение                                     |
| ---------------- | -------------------------------------------- |
| Интерфейс        | USDHC (SDIO)                                 |
| Карта            | microSD, вставляется оператором перед тестом |
| Файловая система | FatFS (SDK middleware)                       |
| Детект карты     | GPIO (CD pin) или опрос через USDHC status   |

### BSP API (предварительно)

```c
/* bsp/usd/include/bsp/usd.h */

typedef enum {
    BSP_USD_OK = 0,
    BSP_USD_ERR_NO_CARD,      /* карта не вставлена */
    BSP_USD_ERR_INIT,         /* USDHC или FatFS init failed */
    BSP_USD_ERR_MOUNT,        /* f_mount() failed */
    BSP_USD_ERR_RW,           /* read/write/compare failed */
} bsp_usd_status_t;

bsp_usd_status_t bsp_usd_init(void);
bsp_usd_status_t bsp_usd_is_card_present(void);
bsp_usd_status_t bsp_usd_test_rw(void);   /* write + read + compare тестового файла */
void             bsp_usd_deinit(void);
```

### test_usd — шаги

| Шаг               | Действие                       | Время    |
| ----------------- | ------------------------------ | -------- |
| 1: Card detect    | `bsp_usd_is_card_present()`    | < 1 мс   |
| 2: Mount          | `f_mount()` — FAT/exFAT        | < 200 мс |
| 3: Write          | Записать 4 KB тестовый файл    | < 500 мс |
| 4: Read + Compare | Прочитать и сравнить побайтово | < 200 мс |
| 5: Unmount        | `f_unmount()`                  | < 50 мс  |

### Интерактивность

Тест помечен `requires_hil = false`, `pre_confirm_prompt = "Вставьте microSD и нажмите OK"`.
test_runner ждёт `{"type":"confirm","id":"usd_insert","confirmed":true}` до вызова `run()`.
Отказ или таймаут 30 с → `TEST_STATUS_SKIP`.

### Файлы

```
bsp/usd/
├── CMakeLists.txt
├── README.md
├── include/bsp/usd.h
└── src/usd.c

firmware/test/src/tests/test_usd.c
```

### CMake

```cmake
# bsp/usd/CMakeLists.txt
target_link_libraries(bsp_usd
    PUBLIC  bsp_status
    PRIVATE bsp_board sdk_usdhc middleware_fatfs
)
```

### Закрытые решения (Этап 4)

- BSP-слой: `bsp_sd` (host init/deinit/card detect) + `firmware_test_fatfs` (FatFS).
  `bsp_usd` как отдельный модуль не создавался — тест работает напрямую через `bsp_sd` + `ff.h`.
- Card detect: `bsp_sd_is_inserted()` через `USDHC_GetPresentStatusFlags`. GPIO-прерывание не используется.
- Карта вставляется оператором по запросу (pre_confirm). Между тестами может извлекаться.
- Drive `2:/`. Тестовый файл `2:/FWTEST.TMP`, удаляется в любом исходе (через `deinit()`).
- Паттерн: `byte[i] = i & 0xFF`, 4096 байт.
- `critical = false`: тест не блокирует HIL-тесты при отсутствии карты.
- Confirm timeout: 30 000 мс (`PROTOCOL_CONFIRM_TIMEOUT_MS`).
- Отдельный `test_usd.h` не создавался — `extern K_TEST_USD` объявлен в `test_runner.c`.

---

## Этап 5 — Display + Button (интерактивные)

### test_display

| Параметр | Значение                             |
| -------- | ------------------------------------ |
| Critical | ❌                                    |
| HIL      | ❌                                    |
| Confirm  | Внутри `run()` — 4 отдельных confirm |

Шаги: заливка Red → confirm → Green → confirm → Blue → confirm → White → confirm.
Каждый шаг посылает `confirm_request`, ждёт `confirm` с таймаутом 15 с.
Итог = AND всех четырёх подтверждений.

`detail` при FAIL содержит ID первого непрошедшего шага: `"display_blue not confirmed"`.

### test_buttons

| Параметр | Значение                              |
| -------- | ------------------------------------- |
| Critical | ❌                                     |
| HIL      | ❌                                     |
| Confirm  | prompt only (детект через bsp_button) |

Шаги: Test_But_1 → Test_But_2. Таргет посылает `confirm_request` как инструкцию
оператору, детектирует нажатие через `bsp_button` — JSON confirm не нужен.
Таймаут 10 с на каждую кнопку.

### Аппаратный контекст кнопок

| Кнопка     | Пин MCU    | GPIO      |
| ---------- | ---------- | --------- |
| Test_But_1 | GPIO_B1_14 | GPIO2[30] |
| Test_But_2 | GPIO_B1_15 | GPIO2[31] |

---

## Этап 6 — CAN + UART + Opto (HIL, M5StampPLC)

Все три теста `requires_hil = true`. Запускаются только при наличии стенда.
BSP для всех трёх уже готов.

### test_can

M5StampPLC отправляет CAN-фрейм → плата принимает → сравниваем ID и payload.

**Шаги:**
1. M5 → `{"cmd":"can_send","id":0x100,"data":[0xDE,0xAD,0xBE,0xEF]}` (через `confirm_request`)
2. Таргет: `uart_cmd("CAN_RECV 500")` → `"100 DEADBEEF"` или `"TIMEOUT"`
3. Ответный: таргет посылает → M5 `can_recv` → верификация

### test_uart_ttl

M5 loopback через UART TTL → echo-верификация.

### test_uart_iso

M5 RLY2 → RS_RX оптовход (BSP_OPTO_CH_RS) → детект ACTIVE/INACTIVE.
Использует `bsp_opto` с `rs_as_gpio=true`.

### test_opto

M5 RLY3/RLY4 → EXT_IN1/IN2 → детект ACTIVE/INACTIVE через `bsp_opto`.

**Параметры стенда (из HIL_BENCH.md):**
```
RLY2 → RS_RX   (BSP_OPTO_CH_RS)  GPIO1[23]
RLY3 → EXT_IN1 (BSP_OPTO_CH_IN1) GPIO1[22]
RLY4 → EXT_IN2 (BSP_OPTO_CH_IN2) GPIO1[21]
```

### HIL pytest для Этапа 6

Тесты firmware_test через USB CDC — отдельные от существующих HIL ELF тестов:

```
tools/hil/
├── conftest.py              ← добавить фикстуру firmware_cdc (USB CDC клиент)
├── 06_test_firmware_can.py  ← M5 + USB CDC
├── 06_test_firmware_uart.py
└── 06_test_firmware_opto.py
```

Фикстура `firmware_cdc` открывает CDC порт firmware_test (прошит в Flash),
посылает JSON команды, читает события. Аналог `uart_cmd` для USB CDC.

---

## Этап 7 — Provisioning

### Что нужно

1. Читать `OCOTP_UNIQUE_ID` (или `OCOTP_MAC0/1`) через SDK fsl_ocotp.
2. Посылать `{"type":"provision_ready","chip_uid":"AABB..."}` после `summary`.
3. Ждать `{"type":"cmd","cmd":"provision_ack"}` от хоста.
4. Записывать статус в Flash (первый сектор после прошивки, вне XIP).

### BSP (предварительно)

```c
/* bsp/provisioning/include/bsp/provisioning.h */
bsp_status_t bsp_prov_read_uid(uint8_t *p_uid, size_t len);  /* 8 байт из OCOTP */
```

### Открытые вопросы

- [ ] Что именно записывать как "пройдено": флаг в Flash или только отправить UID?
- [ ] Нужна ли защита от повторного provisioning (write-once)?

---

## Матрица тестов — итоговая

| ID         | Название       | Тип         | Critical | HIL (M5) | BSP               | Статус |
| ---------- | -------------- | ----------- | -------- | -------- | ----------------- | ------ |
| —          | PING           | cmd         | —        | ❌        | —                 | ✅      |
| `sdram`    | SDRAM 32MB     | self        | ✅        | ❌        | `bsp_sdram` ✅     | ✅      |
| `qspi`     | QSPI Flash     | self        | ✅        | ❌        | `bsp_qspi_flash`✅ | ✅      |
| `usd`      | uSD (SDIO)     | interactive | ❌        | ❌        | `bsp_sd` ✅        | ✅      |
| `display`  | Display RGB888 | interactive | ❌        | ❌        | существующий BSP  | ⬜      |
| `buttons`  | Test_But_1/2   | interactive | ❌        | ❌        | `bsp_button` ✅    | ⬜      |
| `can`      | CAN loopback   | HIL         | ❌        | ✅        | `bsp_can` ✅       | ⬜      |
| `uart_ttl` | UART TTL       | HIL         | ❌        | ✅        | `bsp_uart_host`✅  | ⬜      |
| `uart_iso` | UART ISO +24V  | HIL         | ❌        | ✅        | `bsp_opto` ✅      | ⬜      |
| `opto`     | Opto-in EXT    | HIL         | ❌        | ✅        | `bsp_opto` ✅      | ⬜      |

---

## Зависимости между этапами

```
✅ Этап 1 (протокол v2 + runner)
✅ Этап 2 (bsp_sdram + test_sdram)
✅ Этап 3 (bsp_qspi_flash + test_qspi)
✅ Этап 4 (bsp_sd + test_usd)
⬜ Этап 5 (display + buttons)          ← ТЕКУЩИЙ
⬜ Этап 6 (CAN + UART + Opto, HIL)
⬜ Этап 7 (provisioning)
⬜ Этап 8 (tools/production/ TUI runner) ← параллельно с 6-7
```

---

## Хостовое ПО производственного прогона (Этап 8)

**Решение принято (Вариант D):** отдельное приложение `tools/production/`,
без pytest, с TUI (Textual).

Подробная архитектура описана в предыдущей версии плана (v0.2, раздел
"Открытый вопрос: ПО на стороне хоста").

### Открытые вопросы (перед Этапом 8)

- [ ] TUI: Textual или Rich или plain print на первой итерации?
- [ ] БД: SQLite локально или REST API?
- [ ] Несколько стендов параллельно или всегда один?
- [ ] Этикетка: нужна ли автоматическая печать после provisioning?
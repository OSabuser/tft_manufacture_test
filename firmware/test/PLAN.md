# firmware_test — План разработки

> Версия: 0.5 | Обновлён после завершения Этапа 5 (display + buttons).

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

| Компонент                      | Статус | Примечание                                   |
| ------------------------------ | ------ | -------------------------------------------- |
| `bsp_usb_cdc`                  | ✅      | HIL тест пройден                             |
| firmware_test скелет           | ✅      | `main.c` + `cli.c`                           |
| Протокол v2 + test_runner      | ✅      | JSON-lines event-driven                      |
| `bsp_sdram` + `test_sdram`     | ✅      | 4 фазы: addr/data/seq/retention              |
| `bsp_qspi_flash`               | ✅      | W25Q64/128/256/512, ITCM, IRQ lock           |
| `test_qspi`                    | ✅      | JEDEC + erase + rw + addr range              |
| `bsp_sd` + `test_usd`          | ✅      | bsp_sd + FatFS, pre_confirm, 4 шага          |
| `bsp_display` + `test_display` | ✅      | 4 цвета + ротация, hardware-verified         |
| `bsp_button` + `test_buttons`  | ✅      | 2 кнопки, physical detect, hardware-verified |
| CAN test                       | ⬜      | Этап 6 (bsp_can ✅)                           |
| UART TTL test                  | ⬜      | Этап 6 (bsp_uart_host ✅)                     |
| UART ISO test                  | ⬜      | Этап 6                                       |
| Opto test                      | ⬜      | Этап 6 (bsp_opto ✅)                          |
| Provisioning                   | ⬜      | Этап 7                                       |

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
- **bsp_button_init():** вызывается в `init()` тест-модуля, не в `main.c`.
- **Тест дисплея:** 4 цвета + 2 ротации (TFT ≠ TFT4). Таймаут confirm 15 с → FAIL.
- **Тест кнопок:** физическая детекция через `bsp_button`. Хост не отправляет JSON confirm. Таймаут 10 с → SKIP.

---

## Этап 4 — bsp_sd + test_usd ✅

### Аппаратный контекст

| Параметр         | Значение                                     |
| ---------------- | -------------------------------------------- |
| Интерфейс        | USDHC (SDIO)                                 |
| Карта            | microSD, вставляется оператором перед тестом |
| Файловая система | FatFS (SDK middleware)                       |
| Детект карты     | `USDHC_GetPresentStatusFlags`                |

### Закрытые решения — Этап 4

- BSP-слой: `bsp_sd` (host init/deinit/card detect) + `firmware_test_fatfs` (FatFS).
  `bsp_usd` как отдельный модуль не создавался — тест работает напрямую через `bsp_sd` + `ff.h`.
- Card detect: `bsp_sd_is_inserted()` через `USDHC_GetPresentStatusFlags`. GPIO-прерывание не используется.
- Карта вставляется оператором по запросу (pre_confirm). Между тестами может извлекаться.
- Drive `2:/`. Тестовый файл `2:/FWTEST.TMP`, удаляется в любом исходе (через `deinit()`).
- Паттерн: `byte[i] = i & 0xFF`, 4096 байт.
- `critical = false`: тест не блокирует HIL-тесты при отсутствии карты.
- Confirm timeout: 30 000 мс (`PROTOCOL_CONFIRM_TIMEOUT_MS`).
- `SD_HostInit` не вызывается в `bsp_sd_init()` — `sd_disk_initialize` делает полный init. Двойной init даёт `FR_NOT_READY`.
- Отдельный `test_usd.h` не создавался — `extern K_TEST_USD` объявлен в `test_runner.c`.

---

## Этап 5 — Display + Buttons ✅

### Аппаратный контекст кнопок

| Кнопка     | Пин MCU    | GPIO      | Схема                         | Нажатие |
| ---------- | ---------- | --------- | ----------------------------- | ------- |
| Test_But_1 | GPIO_B1_14 | GPIO2[30] | SWT6x6, pull-up к 3V3 внешний | LOW     |
| Test_But_2 | GPIO_B1_15 | GPIO2[31] | SWT6x6, pull-up к 3V3 внешний | LOW     |

### Закрытые решения — test_display

- `pre_confirm_prompt = NULL` — нет pre-confirm, `test_begin` отправляется сразу.
- 6 шагов confirm: 4 цвета (Red/Green/Blue/White) + 2 ротации (только для TFT ≠ TFT4).
- Таймаут каждого confirm: 15 000 мс. Не подтверждён → FAIL с `detail = "<id> not confirmed"`.
- Ротация: `ROTATE_0` + `FLIP_HORIZONTAL`. Восстановить `ROTATE_0` в любом исходе.
- Фреймбуфер: статический в NonCacheable SDRAM (`AT_NONCACHEABLE_SECTION_ALIGN`, 64-byte align).
- Тип дисплея: `DISPLAY_TEST_TYPE=BSP_DISPLAY_TFT8` через CMake compile definition.

### Закрытые решения — test_buttons

- `pre_confirm_prompt = NULL` — `confirm_request` используется только как UI-подсказка оператору.
- Хост **не** отправляет `{"type":"confirm",...}`. Детект нажатия — через `bsp_button_get_event_pressed()`.
- Таймаут: 10 000 мс → `TEST_STATUS_SKIP` (не FAIL).
- Порядок: But_1 → But_2.
- `bsp_button_poll()` вызывается каждые 5 мс через rate-limiting по `bsp_tick_get_ms()`.
- На каждом poll дренируются события **обеих** кнопок: предотвращает stale-событие от нецелевой кнопки.
- `bsp_button_init()` вызывается в `init()` тест-модуля — сброс debounce-счётчиков перед тестом.
- `bsp_tick_delay_ms()` не используется — polling pattern аналогичен `test_runner_wait_confirm()`.

---

## Этап 6 — CAN + UART + Opto (HIL, M5StampPLC) ← ТЕКУЩИЙ

Все три теста `requires_hil = true`. Запускаются только при наличии стенда.
BSP для всех трёх уже готов.

### test_can

M5StampPLC отправляет CAN-фрейм → плата принимает → сравниваем ID и payload.

**Шаги:**
1. Таргет посылает `confirm_request` → M5 получает команду `can_send`
2. M5 → `{"cmd":"can_send","id":0x100,"data":[0xDE,0xAD,0xBE,0xEF]}`
3. Таргет: ожидает CAN-фрейм, 500 мс → верификация ID и payload
4. Ответный: таргет посылает → M5 `can_recv` → верификация

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
посылает JSON команды, читает события.

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

### Открытые вопросы — Этап 7

- [ ] Что именно записывать как «пройдено»: флаг в Flash или только отправить UID?
- [ ] Нужна ли защита от повторного provisioning (write-once)?

---

## Матрица тестов — итоговая

| ID         | Название       | Тип         | Critical | HIL (M5) | BSP                | Статус |
| ---------- | -------------- | ----------- | -------- | -------- | ------------------ | ------ |
| —          | PING           | cmd         | —        | ❌        | —                  | ✅      |
| `sdram`    | SDRAM 32 MB    | self        | ✅        | ❌        | `bsp_sdram` ✅      | ✅      |
| `qspi`     | QSPI Flash     | self        | ✅        | ❌        | `bsp_qspi_flash` ✅ | ✅      |
| `usd`      | uSD (SDIO)     | interactive | ❌        | ❌        | `bsp_sd` ✅         | ✅      |
| `display`  | Display RGB888 | interactive | ❌        | ❌        | `bsp_display` ✅    | ✅      |
| `buttons`  | Test_But_1/2   | interactive | ❌        | ❌        | `bsp_button` ✅     | ✅      |
| `can`      | CAN loopback   | HIL         | ❌        | ✅        | `bsp_can` ✅        | ⬜      |
| `uart_ttl` | UART TTL       | HIL         | ❌        | ✅        | `bsp_uart_host` ✅  | ⬜      |
| `uart_iso` | UART ISO +24V  | HIL         | ❌        | ✅        | `bsp_opto` ✅       | ⬜      |
| `opto`     | Opto-in EXT    | HIL         | ❌        | ✅        | `bsp_opto` ✅       | ⬜      |

---

## Зависимости между этапами

```
✅ Этап 1 (протокол v2 + runner)
✅ Этап 2 (bsp_sdram + test_sdram)
✅ Этап 3 (bsp_qspi_flash + test_qspi)
✅ Этап 4 (bsp_sd + test_usd)
✅ Этап 5 (display + buttons)
⬜ Этап 6 (CAN + UART + Opto, HIL)          ← ТЕКУЩИЙ
⬜ Этап 7 (provisioning)
⬜ Этап 8 (tools/production/ TUI runner)     ← параллельно с 6-7
```

---

## Хостовое ПО производственного прогона (Этап 8)

**Решение принято (Вариант D):** отдельное приложение `tools/production/`, без pytest, с TUI (Textual).

### Открытые вопросы — Этап 8

- [ ] TUI: Textual или Rich или plain print на первой итерации?
- [ ] БД: SQLite локально или REST API?
- [ ] Несколько стендов параллельно или всегда один?
- [ ] Этикетка: нужна ли автоматическая печать после provisioning?
# bootloader

Загрузчик MIMXRT1052: выбирает и запускает приложение `tft_app` из одного из двух слотов
(MCUboot, Direct-XIP), обновляет его с microSD, восстанавливает плату при зависании образа. Сам
загрузчик прошивается только по USB ROM (blhost) или SWD — в поле не обновляется. Канал диагностики —
USB CDC ACM (JSON-строки).

**Принцип работы** (карта памяти, выбор образа, обновление, версии, даунгрейд, recovery) —
[BOOT_FLOW.md](../../docs/bootloader/BOOT_FLOW.md). LED-индикация — [LED_PATTERNS.md](../../docs/bootloader/LED_PATTERNS.md).

---

## Быстрый старт

### Сборка

```bash
just build::build-bootloader-debug    # bootloader.elf/.bin
just build::hab-bootloader-debug       # HAB-контейнер bootloader_hab.bin
```

### Прошивка

SWD (для итеративной разработки, не требует смены boot-режима платы):

```bash
just host::flash-swd-bootloader-debug
# после прошивки обязателен power cycle платы
```

USB ROM (SDP, плата в режиме Serial Downloader):

```bash
just host::flash bootloader debug
```

### Подключение

```bash
screen /dev/cu.usbmodemXXXX      # macOS; порт свой на каждое подключение
```

```json
→ {"type":"cmd","cmd":"ping"}
← {"type":"pong"}

→ {"type":"cmd","cmd":"get_version"}
← {"type":"version_response","fw":"0.1.0"}
```

USB поднимается на каждой загрузке до обращения к SD, поэтому статусы видны, даже если подключиться
заранее.

### Отладка

VSCode → `🐛 Debug: bootloader` — пересобирает, подключается к GDB-серверу
(`just host::debug-server` должен быть запущен), останавливается на `main`. Под отладчиком аппаратный
watchdog приостановлен, пошаговая отладка сбросами не сбивается.

---

## Архитектура

Загрузчик **не зависит от SDRAM** для своей работы (XIP только из W25Q, без DCD) и без дисплея/RTOS:
инициализация, доступ к QSPI-flash, чтение FatFS с SD, проверка и выбор образа, прыжок. SEMC/SDRAM
трогаются только диагностически (`bsp_sdram_configure()`, boot-time smoke-test) — реально их поднимает
для себя уже само приложение в своём раннем startup.
Линкер жёстко ограничивает код бюджетом области загрузчика (256 КБ) с `ASSERT` на границу Slot A —
превышение становится ошибкой сборки, а не тихим заездом в чужую область.

```text
firmware/bootloader/
├── src/
│   ├── main.c            — точка входа: инициализация → одна попытка загрузки
│   │                       (SD-скан + recovery-гейт + прыжок) → цикл ожидания
│   ├── boot_select.*     — выбор валидного слота и прыжок в выбранный образ
│   ├── slot_version.*    — read-only проверка и чтение версии слота (без побочных
│   │                       эффектов на flash)
│   ├── update_policy.*   — чистая логика «ставить/пропустить» + целевой слот
│   ├── recovery.*        — чистая логика решения recovery (порог / фолбэк / режим)
│   ├── sd_update.*       — оркестрация: смонтировать SD, найти TFT_APP.BIN,
│   │                       установить в целевой слот с потоковой verify-записью
│   ├── cli.*             — построчный IO + диспетчеризация команд
│   ├── protocol.*        — сериализация исходящих событий
│   ├── led_status.*      — словарь LED-паттернов (см. ../../docs/bootloader/LED_PATTERNS.md)
│   ├── dev_sdram_test.*  — [DEV-ONLY, Debug] глубокий тест SDRAM по команде "sdram_test"
│   └── version.h.in      — шаблон версии (CMake → generated/version.h)
│
├── mcuboot_port/         — интеграция bootutil (MCUboot) поверх bsp_qspi_flash:
│                           flash_area_* на 2 слота, конфиг, публичный ключ ECDSA-P256
├── fatfs/                — read-only FatFS для чтения TFT_APP.BIN с карты
└── test_stub/            — самостоятельный подписанный образ-заглушка вместо
                            tft_app для аппаратной проверки загрузчика
```

`update_policy` и `recovery` — чистые функции без доступа к железу, целиком покрыты host-тестами.
`boot_select`, `sd_update`, `flash_map_backend` — тонкий аппаратный слой поверх них.

---

## Протокол

USB CDC ACM, JSON-строки. Входящая строка — до `CLI_LINE_BUF_SIZE` (128) байт, исходящее сообщение —
до `PROTO_BUF_SIZE` (192) байт (несимметрично: `qspi_info`/`sdram_test` длиннее старых сообщений).

**Команды хоста:**

| Команда                                | Ответ                                                                                                                                    |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| `{"type":"cmd","cmd":"ping"}`             | `{"type":"pong"}`                                                                                                                          |
| `{"type":"cmd","cmd":"get_version"}`      | `{"type":"version_response","fw":"X.Y.Z"}`                                                                                                |
| `{"type":"cmd","cmd":"wdog"}`             | `{"type":"wdog","armed":…,"timeout_s":…,"recovered":…,"reset_count":…,"threshold":…}`                                                    |
| `{"type":"cmd","cmd":"smoke_status"}`     | `{"type":"status","state":"smoke_pass"}` / `"smoke_fail"` — результат boot-time smoke-теста SDRAM/SEMC (переспрос, см. ниже)               |
| `{"type":"cmd","cmd":"qspi_info"}`        | `{"type":"qspi_info","chip":"W25Q128","mfr":"0xEF","cap_byte":"0x18","size_mb":16,"pass":true}` (переспрос, см. ниже)                     |
| `{"type":"cmd","cmd":"sdram_test"}` <br> **[DEV-ONLY, Debug-сборка]** | серия из 6 `{"type":"sdram_test","phase":"…","pass":…,"duration_ms":…,"fail_addr":"…","expected":"…","got":"…"}` (`configure`/`address_bus`/`data_bus`/`sequential`/`retention`/`summary`) — блокирует главный цикл на ~4 с. Нет в Release/HAB (`BOOTLOADER_DEV_DIAGNOSTICS`) |

`smoke_status`/`qspi_info` ничего не отвечают, если соответствующий boot-time чек ещё не отработал —
в штатной последовательности `main.c` такого не бывает.

**Исходящие статусы** `{"type":"status","state":"…"}`:

| Состояние        | Когда                                    |
| ---------------- | ------------------------------------------ |
| `waiting_for_sd` | нет валидного слота, ждём карту          |
| `installing`     | идёт запись образа в слот                |
| `update_skipped` | кандидат отклонён по версии              |
| `recovery_mode`  | плата в режиме восстановления            |
| `smoke_pass`     | boot-time smoke-тест SDRAM/SEMC прошёл   |
| `smoke_fail`     | boot-time smoke-тест SDRAM/SEMC провалился |

**Ошибки** `{"ok":false,"error":"…"}`: `SD_CANDIDATE_INVALID`, `SD_INSTALL_WRITE_FAILED`,
`SD_INSTALL_REJECTED`, `SD_DOWNGRADE_ERASE_FAILED`, `PARSE_ERR`, `UNKNOWN_CMD`, `LINE_TOO_LONG`.

**Автоматически на старте, без команды:** `wdog` — если предыдущий сброс был по watchdog
(`recovered:true`); `qspi_info` и `smoke_pass`/`smoke_fail` — сразу после соответствующей проверки.
Все три — best-effort: хост почти никогда не успевает открыть порт к этому моменту (USB enumeration),
поэтому у `qspi_info`/`smoke_status` (но не у одноразового boot-time `wdog`) есть команда-переспрос
в таблице выше.

LED-индикация, соответствующая этим состояниям, — [LED_PATTERNS.md](../../docs/bootloader/LED_PATTERNS.md).

---

## Тесты

**Host** — вся логика без железа (выбор слота, сравнение версий, политика установки, решение
recovery, протокол, CLI) на in-memory flash-фейках:

```bash
just build::test-host
```

**Аппаратный стенд** — сборка и подпись образов-заглушек, замещающих `tft_app` при ручной проверке
на плате (здоровые образы + варианты с зависанием на разных стадиях для проверки восстановления):

```bash
just build::build-mcuboot-stub
```

Чек-листы ручной проверки лежат рядом со стендом в `test_stub/`.

---

## Версионирование

Версия задаётся `project(bootloader VERSION X.Y.Z)` в `CMakeLists.txt` и прокидывается через
`configure_file(src/version.h.in → generated/version.h)` в строку, которую возвращает
`get_version`. `version.h` генерируется, вручную не редактируется.

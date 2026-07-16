# bootloader

Загрузчик MIMXRT1052: выбирает и запускает приложение `tft_app` из одного из двух слотов
(MCUboot, Direct-XIP), обновляет его с microSD, восстанавливает плату при зависании образа. Сам
загрузчик прошивается только по USB ROM (blhost) или SWD — в поле не обновляется. Канал диагностики —
USB CDC ACM (JSON-строки).

**Принцип работы** (карта памяти, выбор образа, обновление, версии, даунгрейд, recovery) —
[BOOT_FLOW.md](BOOT_FLOW.md).

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

Загрузчик работает **без SDRAM** (SEMC поднимает само приложение в своём раннем startup) и без
дисплея/RTOS: инициализация, доступ к QSPI-flash, чтение FatFS с SD, проверка и выбор образа, прыжок.
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

USB CDC ACM, JSON-строки, максимум 128 байт на строку.

**Команды хоста:**

| Команда                              | Ответ                                                                                 |
| ------------------------------------ | ------------------------------------------------------------------------------------- |
| `{"type":"cmd","cmd":"ping"}`        | `{"type":"pong"}`                                                                     |
| `{"type":"cmd","cmd":"get_version"}` | `{"type":"version_response","fw":"X.Y.Z"}`                                            |
| `{"type":"cmd","cmd":"wdog"}`        | `{"type":"wdog","armed":…,"timeout_s":…,"recovered":…,"reset_count":…,"threshold":…}` |

**Исходящие статусы** `{"type":"status","state":"…"}`:

| Состояние        | Когда                           |
| ---------------- | ------------------------------- |
| `waiting_for_sd` | нет валидного слота, ждём карту |
| `installing`     | идёт запись образа в слот       |
| `update_skipped` | кандидат отклонён по версии     |
| `recovery_mode`  | плата в режиме восстановления   |

**Ошибки** `{"ok":false,"error":"…"}`: `SD_CANDIDATE_INVALID`, `SD_INSTALL_WRITE_FAILED`,
`SD_INSTALL_REJECTED`, `SD_DOWNGRADE_ERASE_FAILED`, `PARSE_ERR`, `UNKNOWN_CMD`, `LINE_TOO_LONG`.

Событие `wdog` эмитится также автоматически один раз на старте, если предыдущий сброс был по
watchdog (`recovered:true`).

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

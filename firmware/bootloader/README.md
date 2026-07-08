# bootloader

> Загрузчик MIMXRT1052 — A/Б обновление `tft_app` через microSD (MCUboot,
> Direct-XIP). Сам bootloader обновляется только через USB ROM (blhost) или
> SWD — не в поле. Единственный канал диагностики: USB CDC ACM (JSON-lines,
> тот же стиль протокола, что у `firmware_test`).
>
> Версия: `0.1.0` | Статус: Фаза 1 (скелет) завершена — см. [PLAN.md](PLAN.md)

---

## Содержание

- [Быстрый старт](#быстрый-старт)
- [Архитектура](#архитектура)
- [Карта Flash](#карта-flash)
- [Протокол](#протокол)
- [Roadmap](#roadmap)
- [Версионирование](#версионирование)

---

## Быстрый старт

### 1. Сборка

```bash
just build::build-bootloader-debug
just build::hab-bootloader-debug
```

### 2. Прошивка

Для итеративной разработки — SWD (не требует смены boot-режима платы):

```bash
just host::flash-swd-bootloader-debug
# обязателен power cycle платы после прошивки
```

Через USB ROM (SDP, плата в режиме Serial Downloader):

```bash
just host::flash bootloader debug
```

### 3. Подключение

USB CDC ACM (тот же порядок, что у `firmware_test`):

```bash
# macOS
screen /dev/cu.usbmodemXXXX
```

Проверка связи:

```json
→ {"type":"cmd","cmd":"ping"}
← {"type":"pong"}

→ {"type":"cmd","cmd":"get_version"}
← {"type":"version_response","fw":"0.1.0"}
```

### 4. Отладка

VSCode → `🐛 Debug: bootloader` (`.vscode/launch.json`) — пересобирает через
`build:bootloader-debug`, подключается к GDB-серверу (`just host::debug-server`
должен быть запущен), останавливается на `main`.

---

## Архитектура

Bring-up идентичен `firmware_test` (`firmware/test/src/main.c`), но без
тестового раннера — bootloader не запускает тесты, только диагностический
CLI:

```bash
firmware/bootloader/
├── CMakeLists.txt
└── src/
    ├── main.c              — board_hw_init → led/tick/usb_cdc init →
    │                         ожидание CDC (LED_HEARTBEAT мигает) →
    │                         LED_APP on → главный цикл (poll + cli_process)
    │
    ├── version.h.in        — шаблон версии (CMake → generated/version.h)
    │
    ├── cli.h / cli.c        — IO-слой: буферизация строк, парсинг "type"/"cmd"
    │                         (урезанное подмножество firmware_test/src/cli.c)
    │
    └── protocol.h / .c     — сериализация исходящих событий → cli_send()
```

Bootloader **без SDRAM** — DCD не используется (`bsp_boot_xip_no_dcd` вместо
`bsp_boot_xip`). `tft_app` инициализирует SEMC сама в своём раннем startup
(см. [BOOTLOADER_FLASH_MAP.md](../../docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md)).

Линкер: `cmake/linker/MIMXRT1052xxxxx_bootloader_flexspi_nor.ld` — `m_text`
жёстко ограничен 247 КБ, с `ASSERT` на границу Slot A. Превышение бюджета —
ошибка линковки, а не тихий выход кода за пределы своей области.

---

## Карта Flash

Полная карта, обоснование размеров и принцип "один bootloader на любую
ёмкость чипа" — в
[docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md](../../docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md).

| Область | Смещение | Размер |
|---|---|---|
| Bootloader | `0x60000000` | 256 КБ |
| Slot A (tft_app) | `0x60040000` | 2 МБ |
| Slot Б (tft_app) | `0x60240000` | 2 МБ |
| ФС ассетов (спрайты/музыка) | `0x60440000` | остальное (рантайм) |

---

## Протокол

Транспорт и фреймирование — как у `firmware_test`
([firmware/test/README.md](../test/README.md#протокол-v2)): USB CDC ACM,
JSON-lines, максимум 128 байт на строку.

**Реализовано (Фаза 1):**

| Команда хоста | Ответ |
|---|---|
| `{"type":"cmd","cmd":"ping"}` | `{"type":"pong"}` |
| `{"type":"cmd","cmd":"get_version"}` | `{"type":"version_response","fw":"X.Y.Z"}` |

Ошибки: `{"ok":false,"error":"PARSE_ERR"}` / `"UNKNOWN_CMD"` / `"LINE_TOO_LONG"`.

Без `session_start` — в отличие от `firmware_test`, bootloader не шлёт
приветствие автоматически; живость проверяется явным `ping` (тот же паттерн,
что использует HIL-фикстура `firmware_cdc` для `firmware_test`).

**Появится в следующих фазах** (см. [PLAN.md](PLAN.md)): `status`-события
(`waiting_for_sd`, `installing`, `smoke_pass`/`smoke_fail`, `booting`) —
Фазы 3-4.

---

## Roadmap

Полный план по фазам с целями и критериями верификации — [PLAN.md](PLAN.md).

| Фаза | Что делает |
|---|---|
| 0 ✅ | Карта Flash, регистрация в сборке |
| 1 ✅ | Скелет: bring-up, USB CDC, ping/get_version, LED heartbeat |
| 2 | bootutil (MCUboot Direct-XIP) — выбор слота, верификация подписи |
| 3 | Установка образа с microSD, состояние "нет валидного слота" |
| 4 | SDRAM/W25Q smoke-test, словарь LED-паттернов |
| 5 | HAB Release, интеграция в service-tui |

---

## Версионирование

Версия задаётся `project(bootloader VERSION X.Y.Z)` в `CMakeLists.txt`,
прокидывается через `configure_file(src/version.h.in → generated/version.h)`
в `BOOTLOADER_VERSION_STR` — тот же механизм, что у `firmware_test`
(см. [firmware/test/README.md#версионирование](../test/README.md#версионирование)).

`version.h` генерируется, не редактируется вручную.

# bootloader

> Загрузчик MIMXRT1052 — A/Б обновление `tft_app` через microSD (MCUboot,
> Direct-XIP). Сам bootloader обновляется только через USB ROM (blhost) или
> SWD — не в поле. Единственный канал диагностики: USB CDC ACM (JSON-lines,
> тот же стиль протокола, что у `firmware_test`).
>
> Версия: `0.1.0` | Статус: Фаза 2 (bootutil / MCUboot Direct-XIP) завершена — см. [PLAN.md](PLAN.md)

---

## Содержание

- [bootloader](#bootloader)
  - [Содержание](#содержание)
  - [Быстрый старт](#быстрый-старт)
    - [1. Сборка](#1-сборка)
    - [2. Прошивка](#2-прошивка)
    - [3. Подключение](#3-подключение)
    - [4. Отладка](#4-отладка)
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

CDC поднимается, только если в обоих слотах (A/Б) нет валидного образа — иначе
bootloader сразу выбирает слот и прыгает в `tft_app` (Direct-XIP), не поднимая
USB вообще. Для проверки диагностического режима слоты должны быть пустыми
или содержать только невалидные образы.

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

Bring-up идентичен `firmware_test` (`firmware/test/src/main.c`), но вместо
тестового раннера — сразу попытка выбрать и запустить `tft_app` из одного из
двух слотов (MCUboot Direct-XIP, `bootutil`), и только если валидного образа
нет ни в одном слоте — диагностический CLI:

```bash
firmware/bootloader/
├── CMakeLists.txt
├── mcuboot_port/               — интеграция bootutil (MCUboot) с bsp_qspi_flash
│   ├── flash_map_backend.c     — flash_area_* поверх bsp_qspi_flash (2 слота)
│   ├── keys.c                  — публичный ключ ECDSA-P256 для imgtool-подписи
│   ├── bootutil_sources.cmake  — общий список исходников bootutil+TinyCrypt+
│   │                              ASN.1 (используется и host-тестами)
│   └── mcuboot_config/, sysflash/, flash_map_backend/, keys/ — конфиг-заголовки
│
└── src/
    ├── main.c              — board_hw_init → led/tick/qspi init →
    │                         boot_select_and_jump() (при успехе не
    │                         возвращается) → иначе usb_cdc init → ожидание
    │                         CDC (LED_HEARTBEAT мигает) → LED_APP on →
    │                         главный цикл (poll + cli_process)
    │
    ├── boot_select.c/.h    — обёртка над bootutil: `boot_go()` (выбор
    │                         валидного слота по подписи/версии) + прыжок в
    │                         выбранный образ (`jump_to_image`)
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
жёстко ограничен 247 КБ, с `ASSERT` на границу Slot A.

Верификация образов — подпись ECDSA-P256 (bootutil/TinyCrypt), два независимых
слота (A/Б) по 2 МБ каждый, без swap/scratch (Direct-XIP). Аппаратно
верифицировано все 5 сценариев чек-листа (единственный валидный слот, выбор
более новой версии, откат при повреждении, оба слота пусты, revert
неподтверждённого образа)

---

## Карта Flash

Полная карта, обоснование размеров и принцип "один bootloader на любую
ёмкость чипа" — в
[docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md](../../docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md).

| Область                     | Смещение     | Размер              |
| --------------------------- | ------------ | ------------------- |
| Bootloader                  | `0x60000000` | 256 КБ              |
| Slot A (tft_app)            | `0x60040000` | 2 МБ                |
| Slot Б (tft_app)            | `0x60240000` | 2 МБ                |
| ФС ассетов (спрайты/музыка) | `0x60440000` | остальное (рантайм) |

---

## Протокол

Транспорт и фреймирование — как у `firmware_test`
([firmware/test/README.md](../test/README.md#протокол-v2)): USB CDC ACM,
JSON-lines, максимум 128 байт на строку.

**Реализовано (Фаза 1):**

| Команда хоста                        | Ответ                                      |
| ------------------------------------ | ------------------------------------------ |
| `{"type":"cmd","cmd":"ping"}`        | `{"type":"pong"}`                          |
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

| Фаза | Что делает                                                       |
| ---- | ---------------------------------------------------------------- |
| 0 ✅  | Карта Flash, регистрация в сборке                                |
| 1 ✅  | Скелет: bring-up, USB CDC, ping/get_version, LED heartbeat       |
| 2 ✅  | bootutil (MCUboot Direct-XIP) — выбор слота, верификация подписи |
| 3    | Установка образа с microSD, состояние "нет валидного слота"      |
| 4    | SDRAM/W25Q smoke-test, словарь LED-паттернов                     |
| 5    | HAB Release, интеграция в service-tui                            |

---

## Версионирование

Версия задаётся `project(bootloader VERSION X.Y.Z)` в `CMakeLists.txt`,
прокидывается через `configure_file(src/version.h.in → generated/version.h)`
в `BOOTLOADER_VERSION_STR` — тот же механизм, что у `firmware_test`
(см. [firmware/test/README.md#версионирование](../test/README.md#версионирование)).

`version.h` генерируется, не редактируется вручную.

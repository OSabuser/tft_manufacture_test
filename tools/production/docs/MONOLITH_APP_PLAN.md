# service-tui — миграция на монолит (V4), USB-кроссплатформенность и релиз

> Единый рабочий документ. Объединяет и заменяет `MONOLITH_PLAN.md`
> и `USB_CROSSPLATFORM.md`; заменяет шаг 3 («Упаковка», вариант B
> с venv) в `RELEASE_PLAN.md`. Шаги 1–2 плана релиза (merge, CHANGELOG)
> выполнены и не затрагиваются; шаги 4–6 переезжают в фазы 5–6.

Ветка: `feature-tui-monolith` от `dev`. Каждая фаза = коммит(ы) с
зелёным гейтом; откат любой фазы не ломает предыдущие.

---

## 1. Принятые решения (зафиксировано)

### Р1. Nuitka снят с повестки; упаковка — PyInstaller

Защита исходников — не требование. PyInstaller — официально
поддержанный NXP путь развёртывания spsdk. Один exe, без
`tools_host/.venv`.

### Р2. `tools/host/flash_usb.py` НЕ трогаем

Остаётся dev-CLI для `just host::flash*`, `incoming`, `production`.
TUI получает собственный нативный spsdk-backend. Прецедент закрыт
ранее: `tools/shared/m5_agent.py` сознательно не делался — независимые
клиенты признаны правильным решением, не техдолгом. Бонусы:
мгновенный откат на любой фазе и независимый эталон поведения для
hardware-гейтов.

### Р3. Соответствие CLI → Python API (проверено по документации spsdk)

| Сейчас (subprocess)                                  | Станет (in-process)                                               |
| ---------------------------------------------------- | ----------------------------------------------------------------- |
| `sdphost -u ... write-file 0x20001C00 <flashloader>` | `SDP.write_file(0x20001C00, data)`                                |
| `sdphost -u ... jump-address 0x20001C00`             | `SDP.jump_and_run(0x20001C00)`                                    |
| `blhost -u ... get-property 1 0` (поллинг)           | `McuBoot.get_property(PropertyTag.CURRENT_VERSION)`               |
| `blhost ... fill-memory 0x2000 4 0xC0000007 word`    | `McuBoot.fill_memory(0x2000, 4, 0xC0000007)`                      |
| `blhost ... configure-memory 9 0x2000`               | `McuBoot.configure_memory(0x2000, mem_id=9)`                      |
| `blhost ... flash-erase-region 0x60000000 <size> 0`  | `McuBoot.flash_erase_region(0x60000000, size)`                    |
| `blhost ... write-memory <addr> <file> 0`            | `McuBoot.write_memory(addr, data)`                                |
| `blhost -t 200000 ... flash-erase-all 9`             | `McuBoot.flash_erase_all(mem_id=9)` + таймаут ⚠В2                 |
| `blhost ... reset`                                   | `McuBoot.reset(reopen=False)`                                     |
| `uv run nxpimage hab export -c <yaml> -o <bin>`      | `HabImage` (пакет `spsdk.image.hab`) → `.export()` ⚠В1            |
| Детект SDP/Flashloader                               | `SdpUSBInterface.scan(...)` / `MbootUSBInterface.scan()` (см. Р7) |

### Р4. Потоковая модель

- `flash_backend.py` — чистый синхронный Python, **ноль** импортов
  Textual/asyncio; прогресс — синхронный callback.
- Мост поток→loop живёт **внутри `Flasher`** (не в экранах):
  `asyncio.to_thread(...)` + `asyncio.run_coroutine_threadsafe()`.
- Публичный API `Flasher` заморожен → `flash.py`/`waiting.py` в фазах
  1–3 не редактируются. Главный контейнер регрессии.
- Worker-поток не трогает виджеты (грабли `self._running`/
  `MessagePump` из DEV_ARCH §13 сюда не заносим).

### Р5. Отмену операций сознательно НЕ делаем

Как сейчас: кнопки блокируются `_set_busy`. Блокирующий USB-вызов из
потока корректно не прервать; обрыв кабеля backend обнаружит сам через
исключения spsdk — это и есть заявленный выигрыш V4 вместо
зомби-subprocess.

### Р6. Data-файлы и временные файлы

- Единый источник `tools/host/dcd/` (`ivt_flashloader.bin`, `dcd.bin`,
  `*_fdcb.bin`) — их использует и нетронутый `flash_usb.py`. TUI
  резолвит двухрежимным паттерном (dev: repo-relative; frozen: рядом
  с exe через PyInstaller `datas`). Дублей блобов в репо не заводим.
- Временные HAB-файлы — в `tempfile.gettempdir()`; cwd-магия
  «temp .yaml в `tools/host/hab/`» умирает вместе с subprocess (в
  Python API пути абсолютные). Побочный выигрыш: frozen-бандлу не
  нужна записываемая директория внутри себя.

### Р7. Детект устройств — через spsdk, `pyusb` удаляется ⚠ пересмотр закрытого решения

Пересматривает `_detect_usb` (pyusb) и вытекающее требование
Zadig/WinUSB из `RELEASE_PLAN.md` шаг 4. **Требует твоего явного
подтверждения** — после него считается принятым.

Суть: SDP BootROM (`1FC9:0130`) и Flashloader (`15A2:0073`) — это
**HID**-устройства. spsdk общается с ними через libusbsio/hidapi,
которому Zadig не нужен — именно поэтому sdphost/blhost/SPT у NXP
работают на Windows из коробки. WinUSB был нужен только нашему
pyusb-детекту; привязка WinUSB к HID-устройству вдобавок *отбирает*
его у стандартного HID-стека. Требование Zadig — самонаведённое.

Замена (фаза 1):

- `detect_sdp()` → `SdpUSBInterface.scan(device_id="0x1FC9:0x0130")`;
- детект Flashloader → `MbootUSBInterface.scan()`;
- `detect_cdc()` → только `serial.tools.list_ports` по VID:PID
  (CDC по определению виден как COM-порт; pyusb-ветка ничего не
  добавляла).

Следствия: Zadig исчезает из полевой инструкции целиком; `pyusb` и
`libusb-1.0.dll` уходят из зависимостей/бандла; вместо них в бандл
должны попасть нативные библиотеки libusbsio (гейт фазы 5).

### Р8. Резолв serial-портов: VID:PID — идентичность, имя порта — рантайм

Имена портов не переносимы даже в пределах одной ОС (перевоткнул в
другой USB-порт — имя изменилось: `cu.usbmodemXXXX` / `COMn` /
`ttyACMn`). Принцип:

```
1. Задан <NAME>_PORT в окружении → использовать as-is (escape hatch).
2. Иначе list_ports по VID:PID.
3. Одно совпадение → info.device (pyserial открывает и cu.*, и COMn,
   включая COM>9, без платформенных приседаний).
4. Ноль → «не найдено» (для TUI — штатное состояние WaitingScreen).
5. Несколько → первое + warning в лог; дизамбигуация по
   serial_number — задел на будущее (см. О3).
```

Реализация — новый `tools/production/app/usb_ports.py`. В shared не
выносится (прецедент Р2): HIL-стенд стационарный, пиновка портов в
`.env` там осмысленна и остаётся как есть.

### Р9. Пересмотр `.env` (контекст `tools/production`; HIL-блок не трогается)

| Переменная                                                      | Судьба                                       |
| --------------------------------------------------------------- | -------------------------------------------- |
| `BOOTROM_VID/PID`, `FLASHLOADER_VID/PID`, `SERVICE_CDC_VID/PID` | Остаются (идентичность)                      |
| `SERVICE_M5_VID/PID`                                            | Добавить (сейчас M5 идентифицируется портом) |
| `HIL_USB_CDC_PORT`, `HIL_M5_PORT` (в контексте TUI)             | Необязательный override                      |

Принцип: production-TUI запускается на чистой машине **вообще без
`.env`** — все значения имеют fallback-константы в коде (для VID/PID
уже так). `.env` — инструмент разработчика/стенда, не артефакт рядом
с exe.

---

## 2. Матрица «USB-класс × ОС» (справочная база решений Р7/Р8)

| Устройство                  | VID:PID     | Класс        | macOS                           | Windows 10/11                                                                                        | Linux                                                             |
| --------------------------- | ----------- | ------------ | ------------------------------- | ---------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| BootROM SDP                 | `1FC9:0130` | HID          | Из коробки (IOHIDFamily)        | Из коробки (`hidclass`)                                                                              | Из коробки; права на `hidraw` (udev)                              |
| Flashloader                 | `15A2:0073` | HID          | Из коробки                      | Из коробки                                                                                           | То же                                                             |
| firmware_test               | `1996:00AD` | CDC ACM      | Из коробки, `/dev/cu.usbmodem*` | Из коробки с Win10 (`usbser.sys` по классу), `COMn`                                                  | Из коробки, `/dev/ttyACM*`, dialout (рецепт `setup-m5-udev` есть) |
| M5StampPLC                  | см. О1      | CDC или мост | см. О1                          | см. О1                                                                                               | —                                                                 |
| `pyusb`/libusb перечисление | —           | —            | Работает (текущий детект)       | **Не видит без WinUSB/libusbK** (задокументировано в RELEASE_PLAN) + нужен `libusb-1.0.dll` в бандле | Работает при правах                                               |

macOS-нюанс, снимаемый резолвером Р8 автоматически: использовать
`cu.*`, не `tty.*` (callout не ждёт DCD) — `list_ports` на macOS и
так отдаёт `cu.*`.

---

## 3. Открытые вопросы (требуют ответа/измерения до соответствующей фазы)

**О1. M5StampPLC — чем представляется хосту?** Честно: не знаю —
нативный ESP32-S3 USB CDC (VID Espressif `303A`) или мост
CH9102/CP210x. Снимается за минуту: воткнуть M5, выполнить
`python -m serial.tools.list_ports -v` → VID:PID + serial_number.
Развилка: нативный CDC → драйверы не нужны нигде, Р8 закрывает
вопрос; мост → на изолированной Windows без сети нужен один
вендорский драйвер (честная необходимость уровня ОС, в отличие от
Zadig), пункт в инструкцию сервисника + проверка на гейте 5.
**Измерение — в фазе 0.**

**О2. Состав `firmware/` в релизе v1** — унаследован из
`RELEASE_PLAN.md`: только `firmware_test` Debug (Release нестабилен)?
`bootloader`+`app` не включаем? Нужен для фазы 5, не раньше.

**О3. Несколько одинаковых устройств одновременно** — сознательно за
скобками v1: у SDP BootROM различающего серийника нет, UX
WaitingScreen рассчитан на одну плату. Фиксируется в README как
известное ограничение, не как долг. Подтверди, что объём согласован.

---

## Фаза 0 — Спайк / де-риск (без изменений в TUI)

Цель: подтвердить неизвестные API (⚠В1, ⚠В2, сигнатуры `scan()`)
и совместимость зависимостей — до первой строки боевого кода.

**Файлы:** `tools/production/pyproject.toml`, `uv.lock`,
`tools/production/spike/` (временная директория, в релиз не идёт).

1. Добавить `spsdk==3.7.0` в `tools/production`, `uv lock`/`uv sync` —
   дерево (156 пакетов в `tools/host/uv.lock`) не должно конфликтовать
   с `textual`/`pyserial`.
2. `spike_hab.py`: HAB из «сырого» бинарника через `HabImage` с теми же
   опциями, что в `_build_custom_hab()` (`startAddress=0x60000000,
   ivtOffset=0x1000, initialLoadSize=0x2000, family=mimxrt1050`;
   DCD on/off). — закрывает ⚠В1.
3. `spike_flash.py`: `SdpUSBInterface.scan` → `SDP.write_file` +
   `jump_and_run` → поллинг `McuBoot.get_property` →
   `configure_memory`; выяснить механизм таймаута ≥200 с для
   `flash_erase_all` (эквивалент `blhost -t 200000`). — закрывает ⚠В2
   и сигнатуры Р7.
4. Прогнать `spike_flash.py` на **Windows-машине без Zadig** —
   дешёвая ранняя проверка Р7 (детект + HID-транспорт).
5. Измерение О1 (VID:PID/serial M5 через list_ports).

### Гейт 0

- [ ] `uv lock` без конфликтов.
- [ ] **Golden-тест HAB (byte-exact):** образ из `HabImage` побайтно
      равен `nxpimage hab export` с тем же конфигом, DCD on/off.
      Железо не нужно. Оформить pytest'ом — остаётся навсегда как
      регрессия на апгрейды spsdk.
- [ ] Железо: flashloader поднимается через Python API,
      `get_property` отвечает.
- [ ] Windows без Zadig: SDP виден, flashloader грузится.
- [ ] Известен способ задать таймаут ≥200 с для erase-all.
- [ ] О1 закрыт (VID:PID зафиксирован, ветка развилки известна).
- [ ] **Стоп-условие В1:** `HabImage` не даёт byte-exact / API
      непригоден → HAB остаётся subprocess-вызовом `nxpimage`
      in-process; остальной монолит не страдает; фаза 3 сужается.
      Решение фиксируется до старта фазы 1.

---

## Фаза 1 — Backend-модуль (синхронное ядро, без UI)

**Файлы (новые):** `tools/production/app/flash_backend.py`,
`tools/production/app/usb_ports.py`,
`tools/production/tests/test_flash_backend.py`.
**Файлы (правки):** нет — `flasher.py`, `flash.py` не трогаются.

`flash_backend.py` — прямой перенос логики `flash_usb.py` по таблице Р3:

- `detect_sdp()/detect_cdc()` — по Р7 (spsdk scan + list_ports),
  pyusb-код не переносится;
- `load_flashloader(progress)` — идемпотентно, как сейчас («уже
  запущен — пропускаем»), поллинг с тем же 10-секундным лимитом;
- `configure_flexspi()`, `write_fcb()`, `write_fcb_explicit(path)` —
  1:1 с `flash_usb.py`, включая option words `0xC0000007`/`0xF000000F`
  и расчёт `erase_size` по 4K-секторам;
- `flash_image(hab_bin, fcb_path|None, progress)`, `erase_chip(progress)`;
- прогресс: `Callable[[FlashProgress], None]`, фазы — честные этапы
  конвейера (`flashloader/configure/erase/fcb/write/reset`) вместо
  regex-парсинга stdout. Если спайк подтвердил `progress_callback`
  у записи — процент внутри `write_memory`, иначе поэтапный
  (5 этапов ≈ 20% гранулярность — приемлемо);
- ошибки: доменное `FlashBackendError(phase, cause)`; внутри перехват
  `SdpError`/`McuBootError`/`McuBootConnectionError`; таймаут и
  «устройство пропало» различимы.

`usb_ports.py` — резолвер Р8 (`UsbId`, `resolve_serial_port`).

### Гейт 1

- [ ] Unit-тесты (без железа): мок `McuBoot`/`SDP`, сверка
      последовательности команд с `flash_usb.py` как эталоном для
      flash/erase/fcb-explicit; исключения → `FlashBackendError`
      с корректной фазой; резолвер портов (override / одно /
      ноль / несколько совпадений).
- [ ] Smoke на железе через mini-CLI (`python -m app.flash_backend`):
      прошивка `firmware_test_hab.bin` (Debug), плата грузится,
      текущий TUI (subprocess-версия!) видит CDC, `ping→pong`.
- [ ] Chip erase на W25Q512 укладывается в таймаут.

---

## Фаза 2 — Пересадка `Flasher` на backend (async-фасад)

**Файлы (правки):** `tools/production/app/flasher.py` — переписывается
изнутри при неизменном публичном API.
**Не трогаются:** `flash.py`, `waiting.py`, `app.py`,
`connection_watcher.py`, `models.py`.

- `flash()/erase_chip()`: вместо `uv run ...` и `_run_cmd` —
  `await asyncio.to_thread(backend..., ...)`; loop захватывается до
  ухода в поток, прогресс пробрасывается через
  `run_coroutine_threadsafe(progress_cb(p), loop)`.
- `detect_sdp()/detect_cdc()/list_custom_binaries()` — делегирование
  в backend, сигнатуры и `@staticmethod` прежние.
- PRODUCTION-цепочка (bootloader → app при успехе) остаётся в
  `Flasher.flash()`.
- Удаляются: `_run_cmd`, `_run_flash`, `_run_flash_bin`,
  `_parse_progress`, `_RE_PERCENT`, `_RE_PHASE`, пути `uv`/скрипта.
  `_run_flash_custom`/`_build_custom_hab` пока на subprocess
  (мигрируют в фазе 3) — смешанный режим допустим, API этого не видит.
- Резолв `BUILD_DIR` HAB-образов переезжает в backend: dev —
  `<repo>/build/<Type>/<name>_hab.bin`, frozen —
  `sys.executable.parent / "firmware"` (схема из RELEASE_PLAN §3,
  теперь без venv).

### Гейт 2

- [ ] `git diff` подтверждает: `flash.py` не изменён ни на строку.
- [ ] Headless Textual-тест: FlashScreen — прогресс обновляется,
      кнопки блокируются/разблокируются, `FlashDone` с корректными
      полями.
- [ ] Железо: полный цикл через TUI — firmware_test →
      PostFlashScreen → диагностика; PRODUCTION (два образа подряд);
      chip erase. Поведение визуально эквивалентно subprocess-версии.
- [ ] `#flash-log` не «зависает» на долгих этапах.

---

## Фаза 3 — Кастомные бинарники: HAB in-process + явный FCB

**Файлы (правки):** `flash_backend.py` (+`build_custom_hab()`),
`flasher.py` (custom-путь → backend).
**Не трогается:** `flash.py` (UI custom-группы готов).

- `build_custom_hab(raw_bin, use_dcd)`: конфиг формируется в памяти,
  `DCDFilePath` — абсолютным путём через резолвер Р6; временный образ —
  в системном tmp. Если сработало стоп-условие В1 — та же сигнатура,
  внутри subprocess `nxpimage`; UI разницы не видит.
- `_run_flash_custom`: `build_custom_hab` → `flash_image(hab,
  fcb_path=dcd/<variant>_fdcb.bin)` — вся цепочка in-process
  (`write_fcb_explicit` готов с фазы 1).
- Golden-тест фазы 0 расширяется custom-кейсом (реальный
  легаси-бинарник, DCD on/off).

### Гейт 3 (повторяет чек-лист RELEASE_PLAN шага 4 по custom-пути)

- [ ] Golden-тест HAB зелёный для custom-кейса.
- [ ] Железо W25Q128: custom, DCD off → грузится.
- [ ] Железо W25Q512: custom → грузится; якорная проверка 4-байтной
      адресации (aliasing-методика) в порядке.
- [ ] Chip erase → повторная прошивка → плата живая.
- [ ] «Липкий» `FlashPreset` работает (следующая плата — выбор
      подставлен).

---

## Фаза 4 — Нативная обработка отвала USB + зачистка

**Файлы (правки):** `flash_backend.py`, `flasher.py`; точечно
`flash.py`/`connection_watcher.py` — только если гейт покажет
необходимость (по умолчанию нет).

- Обрыв посреди операции: `McuBootConnectionError`/таймауты →
  `FlashBackendError(..., connection_lost=True)` → `Flasher` возвращает
  `False` + финальный `FlashProgress(phase="error")` с
  человекочитаемым сообщением. Схема с `ConnectionWatcherMixin`
  прежняя: во время `_flashing` watcher приглушён, обрыв репортит сам
  backend — то, что раньше делал subprocess, без зомби-процессов.
- Ревизия ресурсов: USB-интерфейсы закрываются в
  `finally`/context-manager'ах при любом исходе (утечка HID-хэндла —
  классическая причина «device busy» при повторе).
- Зачистка: следов subprocess-эры, `uv`, путей `flash_usb.py`,
  `_HAB_DIR`-магии в `tools/production/` не остаётся.

### Гейт 4 (деструктивные сценарии на железе)

- [ ] Выдернуть USB во время `write-memory` → ошибка в TUI, возврат
      на WaitingScreen, повторная вставка → повторная прошивка
      успешна (порт не «занят»).
- [ ] Выдернуть во время chip erase (W25Q512) → то же; выход из
      приложения чистый, подвисших потоков нет.
- [ ] Выдернуть в простое на FlashScreen → срабатывает watcher
      (регрессия старого пути).
- [ ] `grep -r "flash_usb\|uv run\|subprocess\|usb.core" \
      tools/production/app/` — пусто.

---

## Фаза 5 — Упаковка PyInstaller (замена шага 3 RELEASE_PLAN)

**Файлы (новые):** `tools/production/service_tui.spec`, рецепты в
`just/ci.just` или `host.just` (по месту; имена задач согласуем
отдельно, не изобретаю).

```
service-tui-vX.Y.Z-<os>/
├── service_tui[.exe]        ← PyInstaller, onedir (onefile на Windows
│                               замедляет старт распаковкой — не берём)
├── _internal/               ← рантайм PyInstaller
│   └── data/                ← dcd.bin, *_fdcb.bin, ivt_flashloader.bin
│                               (datas из tools/host/dcd/), data spsdk
├── firmware/
│   └── Debug/firmware_test_hab.bin      (состав — см. О2)
└── custom_binaries/          ← пустая, создаётся и так
```

Ключевые пункты spec:

- `collect_data_files("spsdk")` (+ при необходимости
  `SPSDK_DATA_FOLDER` — документированный NXP механизм для frozen);
- **`collect_dynamic_libs("libusbsio")`** — нативный HID-транспорт
  spsdk (следствие Р7); `libusb-1.0.*` в бандле отсутствует;
- `datas`: `tools/host/dcd/{dcd.bin, w25q128_fdcb.bin,
  w25q512_fdcb.bin, ivt_flashloader.bin}` → `data/`;
- резолвер путей backend'а: frozen → `data/` рядом с exe;
- версия: `_read_app_version()` (tomllib) — `pyproject.toml` в
  `datas`, проверить чтение во frozen.

### Гейт 5 (замена шага 4 RELEASE_PLAN; Windows + macOS)

- [ ] Чистая Windows-машина, **без Zadig, без сети, без Python/uv**:
      полный полевой цикл — детект SDP → firmware_test → диагностика →
      custom (128 и 512) → chip erase.
- [ ] Если О1 = мост: установка одного вендорского драйвера по
      инструкции, M5-функции работают.
- [ ] То же на macOS (в ветке «мост» — проверить и там).
- [ ] Версия на WaitingScreen корректна во frozen.
- [ ] Порты резолвятся при перетыкании в другой физический USB-порт
      (проверка Р8 на обеих ОС).

---

## Фаза 6 — Документация, CHANGELOG, релиз (шаги 5–6 RELEASE_PLAN)

**Файлы:** `CHANGELOG.md`; `RELEASE_PLAN.md` (закрыть шаг 3 ссылкой
сюда); `docs/DEV_ARCH.md` (§2 — убрать `subprocess uv run` из
диаграммы, §8.3 — новый конвейер); `HOW_TO_FLASH.md`; `README.md`
`tools/production`; `.env.example` (по Р9).

- CHANGELOG: монолит (flash_backend, отказ от venv/subprocess),
  нативный детект без Zadig, кроссплатформенный резолв портов,
  нативная обработка отвала USB, упаковка одним exe.
- Zadig-инструкция в доки **не добавляется** (RELEASE_PLAN планировал
  добавить — отменено по Р7); при ветке О1-«мост» — добавляется
  инструкция по одному вендорскому драйверу.
- Зафиксировать разделение: `flash_usb.py` — dev-CLI (just-рецепты),
  `flash_backend.py` — production-TUI; независимые реализации по
  прецеденту M5-клиентов (Р2).
- Golden-тест HAB — обязательный при апгрейде spsdk.
- Ограничение «одна плата на столе» (О3) — в README.
- Тег релиза = версия из `pyproject.toml`.

### Гейт 6

- [ ] Документация синхронизирована (железо подтверждено гейтами 3–5).
- [ ] `just host::flash*`, `incoming`, `production` работают как
      раньше — регрессия dev-пути.
- [ ] Релизный артефакт собран из тега; чек-лист гейта 5 повторён на
      релизном бинаре.

---

## Сводка рисков и trade-offs

| Риск / trade-off                                    | Фаза | Митигация / цена                                                                                                            |
| --------------------------------------------------- | ---- | --------------------------------------------------------------------------------------------------------------------------- |
| `HabImage` в 3.7.0 не byte-exact / непригоден       | 0    | Стоп-условие В1: HAB остаётся subprocess in-process; монолит не страдает                                                    |
| Таймаут `flash_erase_all` (W25Q512)                 | 0    | Спайк ⚠В2 до боевого кода                                                                                                   |
| Сигнатуры `scan()` / поведение HID-скана на Windows | 0    | Спайк на Windows без Zadig                                                                                                  |
| Гранулярность прогресса хуже stdout-парсинга        | 1–2  | Поэтапный прогресс (5 фаз); честнее текущего — `flash_usb.py` процентов фактически не печатает, бар и сейчас живёт на фазах |
| Утечка USB-хэндла → «device busy»                   | 4    | Context-managers + деструктивный гейт 4                                                                                     |
| PyInstaller: data/hooks spsdk, нативные libusbsio   | 5    | Документированный NXP путь + `SPSDK_DATA_FOLDER` + `collect_dynamic_libs`; риск смещён на CI, не в поле                     |
| Textual-грабли при потоках                          | 2    | Мост изолирован во `Flasher`; экраны не трогаются до фазы 4                                                                 |
| Отказ от отмены операций (Р5)                       | —    | Цена: UX как сегодня (ждать до конца/обрыва); выигрыш: нет некорректного прерывания USB-транзакций                          |
| Первое-из-нескольких при дубликатах устройств (О3)  | —    | Warning в лог; serial_number-дизамбигуация — задел                                                                          |

## Порядок ревью

По практике проекта — один файл за раз; полные файлы там, где файл
новый или переписывается целиком (`flash_backend.py`, `usb_ports.py`,
`flasher.py`), unified diff — для точечных правок (`pyproject.toml`,
документация, `.env.example`).

**Старт (после подтверждения Р7 и, желательно, измерения О1):**
фаза 0 — diff `pyproject.toml` + `spike_hab.py` + `spike_flash.py`.
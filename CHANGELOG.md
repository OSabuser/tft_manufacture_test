# Журнал изменений: `tft_manufacture_test`

Репозиторий: <https://github.com/OSabuser/tft_manufacture_test.git>  
Отслеживаемая ветка: `dev`  
Базовый диапазон: 2026-03-05 .. 2026-05-08, 52 коммита  
Базовый HEAD: `22f98c311a4564d8f18ed72d11635bf908046b1e`  
Версия формата: `strict-2026-05-13`

Этот журнал фиксирует только смысловые инженерные изменения. Формат основан на подходе Keep a Changelog и адаптирован под embedded firmware-репозиторий, где документация, тесты, HIL, CI и изменения vendor-SDK являются важными сигналами состояния проекта.

## Политика формата

Каждая будущая запись должна иметь такую структуру:

```md
## [Не выпущено] или [ГГГГ-ММ-ДД] — короткое название

Диапазон: `old_sha..new_sha`  
Сравнение: https://github.com/OSabuser/tft_manufacture_test/compare/old_sha...new_sha

### Кратко
- Один-два содержательных пункта о том, что изменилось и почему это важно.

### Добавлено
- Новые возможности, модули, тесты, документы или инструменты.

### Изменено
- Поведение, архитектура, сборка, интерфейсы или крупные переработки документации.

### Исправлено
- Исправления ошибок, сборки, тестов и регрессий.

### Тесты
- Host-тесты, HIL-тесты, firmware-test модули, фикстуры, test runners и изменения покрытия.

### CI
- GitHub Actions, локальные CI-скрипты, self-hosted/HIL runners и изменения статуса workflow.

### Документация
- README и изменения архитектурных/protocol/how-to документов, которые влияют на понимание проекта.

### Удалено
- Удаления, имеющие практический смысл.

### Отфильтрованный шум
- Форматирование, чистые перемещения, vendor-импорты или generated churn, которые намеренно не считаются продуктовым изменением.
```

Пустую категорию можно опускать, если в ней нет смысловой информации. Не добавлять raw commit spam, статистику строк, длинные списки файлов или переименования, если они не меняют сборку, тестирование, документацию или использование проекта.

## [Не выпущено]

### Что отслеживать

- ~~Проверять, остаётся ли `.github/workflows/ci.yml` только сборочным workflow~~ — решено: job `test` уже запускает host-тесты (`just ci::test` → `just build::test-host-release`).
- Отслеживать появление self-hosted runner, регулярных аппаратных прогонов или отчётов по HIL.
- Проверять новые firmware-test модули в `firmware/test/src/tests/` и синхронные обновления протокола/документации.
- Следить за развитием BSP: RGB (частично закрыто display-тестом); `tft_app` — директория всё ещё не заведена (bootloader — реализован, Фазы 0–6, см. запись ниже).
- Отслеживать локальные патчи поверх vendor SDK, которые нужно вести отдельным patch log (пример — точечный патч `fault_injection_hardening.c` под `#if defined(__arm__)`, bootloader Фаза 3, см. запись ниже).
- ~~Отслеживать мерж ветки `feature-tui-monolith` в `dev`~~ — смёржено (`b4c664f`), запись закрыта датой ниже.
- Отслеживать тег `bootloader-v*` — после первого релиза закрыть запись «bootloader: полная реализация» датой и финальным SHA (сейчас часть диапазона — незакоммиченные изменения рабочего дерева).

## [Не выпущено] — bootloader: полная реализация, Фазы 0–6 (MCUboot Direct-XIP, HAB, service-tui интеграция)

Диапазон: `4644f21507d3..6c564f16388a` + незакоммиченные изменения рабочего дерева (HAB-подпись
тестовым ключом, Тир-0/Тир-1 верификация в service-tui, CI `bootloader-v*`, документация)
Сравнение: <https://github.com/OSabuser/tft_manufacture_test/compare/4644f21...6c564f1>

> `firmware/bootloader/` была пустой директорией на момент базового среза (см. "Что отслеживать"
> выше — теперь снята с наблюдения, кроме тега релиза). Диапазон охватывает всю реализацию с нуля до
> готовности к первому релизу (`VERSION 1.0.0`), шесть фаз согласно (уже удалённому после завершения,
> см. "Удалено" ниже) `firmware/bootloader/PLAN.md`.

### Кратко

- Загрузчик MIMXRT1052 реализован целиком: XIP из Flash, выбор и запуск `tft_app` из одного из двух
  слотов (MCUboot Direct-XIP), обновление с microSD, устойчивость к зависшим образам (watchdog +
  recovery), HAB-подпись Release-сборки, интеграция с `service-tui` для контроля производственной
  прошивки.
- По пути на реальном железе найдено и исправлено более десятка багов — от неверной трактовки
  регистров FlexSPI/SRC до архитектурных пробелов в чек-листах верификации; детали по фазам ниже.
- `firmware/bootloader/CMakeLists.txt` → `VERSION 1.0.0`; все 6 фаз аппаратно верифицированы.

### Добавлено

- **Фаза 0 — карта Flash.** `docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md` — смещения
  `BOOTLOADER`/`SLOT_A`/`SLOT_B`, зафиксированы до написания кода.
- **Фаза 1 — скелет.** `firmware/bootloader/{CMakeLists.txt,src/main.c,src/cli.c,src/protocol.{c,h}}` —
  bring-up (LED/tick/USB CDC), урезанный протокол (`ping`/`get_version`), HAB unsigned Debug-конфиг.
- **Фаза 2 — bootutil (Direct-XIP).** `mcuboot_port/` — шим `flash_area_*` над `bsp_qspi_flash`,
  `sysflash.h`, `mcuboot_config.h` (TinyCrypt ECDSA-P256, `MCUBOOT_DIRECT_XIP_REVERT`),
  `src/boot_select.{c,h}`. `test_stub/` — заглушка `tft_app` (два слота, разная линковка) для
  аппаратной проверки выбора слота.
- **Фаза 3 — SD-путь установки.** `src/{update_policy,slot_version,sd_update}.{c,h}` — сканирование
  microSD, установка в неактивный слот, top-level состояние «нет валидного образа»;
  `bootloader_fatfs` (read-only FatFS); аппаратный watchdog (`bsp/wdog`).
- **Фаза 4 — SDRAM/QSPI smoke-test.** `bsp/sdram::bsp_sdram_configure()` — C-порт DCD (SEMC/CCM);
  `bsp_qspi_decode_chip()` — идентификация чипа по JEDEC; `src/led_status.{c,h}` — единый словарь
  LED-паттернов; `dev_sdram_test.c` (dev-only, `BOOTLOADER_DEV_DIAGNOSTICS`).
- **Фаза 6 — recovery.** `bsp/boot_state` — счётчик попыток загрузки в `SRC_GPR3` (переживает
  watchdog-сброс, обнуляется на POR); `src/recovery.{c,h}` — чистая функция `recovery_decide()`
  (таксономия отказов A–D); recovery-режим по `BSP_BUTTON_2` с ослабленным version-gate.
- **Фаза 5 — HAB Release + service-tui.** Тестовый HAB-ключ (`tools/host/hab/keys/`, схема NOCAK) —
  `hab_bootloader_release.yaml` реально подписывает Release-образ (`flags=0x08`);
  `tools/service_tui/app/bootloader_client.py` — CDC-клиент bootloader
  (`get_smoke_status`/`get_qspi_info`); `tools/service_tui/app/screens/verify.py` — экран живой
  проверки загрузчика после серийной прошивки (Тир-1); Тир-0 (readback-верификация записи,
  `flash_backend._verify_written()`) — включена по умолчанию для любой прошивки через `service-tui`;
  `firmware/bootloader/SIGNING_CEREMONY.md` — план настоящей production-подписи (HAB SRK + MCUboot
  production-ключ) на будущее.

### Изменено

- **Фаза 3**: детект SD консолидирован на единый `PRSSTAT`; ранний сэмпл кнопки даунгрейда.
- **Фаза 4**: `board_mpu_init()` (общий для всех прошивок код) — добавлен Region 11 под NIC-301
  GPV-регистры (`0x41000000`, 8 МБ).
- **Фаза 5**: `Flasher.PRODUCTION` (`tools/service_tui/app/flasher.py`) сужен до сценария A (только
  загрузчик) — бандл с `tft_app` (сценарий B) отложен до реализации `tft_app`; production жёстко
  резолвит Release, независимо от переменной `FIRMWARE_BUILD_TYPE`. `just host::package-tui` —
  новый явный гвард на `build/Release/bootloader_hab.bin`. `.github/workflows/release.yml` — тег
  `bootloader-v*` (симметрично `firmware-v*`), общий job `firmware` теперь собирает и подписывает
  Release-образ bootloader.
- Корневой `README.md` — статус bootloader `запланирован` → `реализован, v1.0.0`.

### Исправлено

- **Фаза 2** (3 бага, аппаратная верификация): `jump_to_image()` маскировал IRQ перед прыжком (не по
  референсу NXP) — вешал `bsp_delay()` в любом целевом образе; `bsp_qspi_read()` не округлял
  `IDATSZ` до кратного 4 при IP-чтении — контроллер недодавал слово на хвостах не кратной длины
  (впервые проявилось на чтении хэша образа bootutil); `qspi_read_tail()` сравнивал
  `IPRXFSTS.FILL` (watermark-юниты по 8 байт) напрямую со счётчиком слов — зависал на хвостах ровно
  в 2 слова (чтение подписи ECDSA).
- **Фаза 3**: форсированный даунгрейд физически записывался, но не загружался бы (`boot_go()` всегда
  выбирает более высокую версию) — добавлено поле `erase_previous_active`; `fih_panic_loop()`
  (вендоренный bootutil) ронял `test-host-release` в CI на x86_64-раннере (`invalid instruction
  mnemonic 'b'` — ARM/Thumb-only мнемоника, на arm64 devcontainer случайно ассемблировалась,
  маскируя проблему); стабы `test_stub` не позиционно-независимы — линковка под конкретный слот
  обязательна и для реального `tft_app`, не только для заглушки.
- **Фаза 6** (4 бага): счётчик попыток загрузки рос и на пустой плате без SD (без реального
  зависания) — ошибочно уводил бы в recovery через ~4.5 с в штатном ожидании; в стенде `test_stub`
  health-mark вызывался безусловно до проверки `HANG_MODE`, из-за чего счётчик никогда не
  накапливался выше 1 (фолбэк не срабатывал); в самом чек-листе Фазы 6 предписывался файл для
  чужого слота при проверке recovery-установки; `attempt_boot()` не инкрементировал счётчик перед
  первым прыжком в свежеустановленный recovery-образ (симметрия с обычным путём).
- **Фаза 4** (3 бага): AXI-QoS регистры (NIC-301 GPV) валили C-код фолтом — не покрыты
  `board_mpu_init()`, DCD успевал их записать до включения MPU, C-порт — нет; результаты
  smoke-теста терялись (шлются один раз сразу после `init()`, хост не успевает открыть порт) —
  кэширование + переспрос по команде; оценка длительности SDRAM-теста в комментарии оригинала
  завышена ~в 6 раз (реальный прогон ~4.2 с, не ~30 с).
- **Фаза 5** (2 бага в `service-tui`, до публикации): production шил bootloader и (будущий) app по
  одному адресу `FLASH_BASE` — второй шаг затёр бы первый (наследие до-bootloader архитектуры, для
  Direct-XIP неверно); production мог тихо взять unsigned Debug-образ bootloader через
  `FIRMWARE_BUILD_TYPE` (переменная предназначена только для firmware_test, дефолт `Debug`) — теперь
  Release резолвится жёстко.

### Тесты

- Host-тесты выросли с 13 (Фаза 2) до 16 (Фазы 3/6: `update_policy`, `slot_version`, `recovery`) —
  зелёные, Debug и Release, обе платформы (macOS + devcontainer Linux).
- `tools/service_tui`: 68 → 76 тестов (Фаза 5) — Тир-0 readback (`test_flash_backend.py`) + новый
  `test_bootloader_client.py`.
- Полный аппаратный чек-лист пройден на каждой фазе (Фазы 2–6); детали были в удалённых
  `HARDWARE_VERIFICATION_*.md`/`DEBUG_LOG_*.md` (см. git-история, "Удалено" ниже).

### CI

- `.github/workflows/release.yml`: новый job `publish-bootloader` (тег `bootloader-v*`), общий job
  `firmware` расширен на сборку Release HAB bootloader; `service-tui-{macos,windows}` теперь
  докачивают `bootloader_hab.bin` в `build/Release/` перед упаковкой — без этого `just
  host::package-tui` падал бы с новым гвардом (см. "Изменено").
- CI-баг `fih_panic_loop`/x86_64 (Фаза 3, см. "Исправлено") — точечный патч вендоренного
  `fault_injection_hardening.c` под `#if defined(__arm__)`.

### Документация

- `docs/bootloader/HAB_GUIDE.md` — новый §5.1 (разбор команд CSF-секции `nxpimage`, NOCAK vs полная
  SRK-иерархия).
- `firmware/bootloader/SIGNING_CEREMONY.md` — новый план настоящей production-подписи (HAB
  SRK-церемония + MCUboot production-ключ), на будущее.
- `docs/DEV_ARCH.md` (корневой) — исправлена фактическая ошибка (bootloader описывался как
  «копирование в ITCM», реально XIP без ITCM/DCD) и устаревший путь `tools/production/`.
- `tools/service_tui/docs/DEV_ARCH.md` → переименован в `tools/service_tui/docs/ARCHITECTURE.md`
  (коллизия имени с корневым `docs/DEV_ARCH.md`); новый §16 (Тир-0/Тир-1); синхронизирован со всеми
  изменениями Фазы 5; починены 10 битых/несогласованных ссылок на файл в трёх других README.
- `docs/CI_WORKFLOW.md` — синхронизирован с `bootloader-v*` (диаграмма, триггеры, таблица job'ов).

### Удалено

- `firmware/bootloader/PLAN.md`, `DEBUG_LOG_PHASE2.md`, `DEBUG_LOG_PHASE3_SD.md`,
  `test_stub/HARDWARE_VERIFICATION_{PHASE2,PHASE3,PHASE6,LED_PATTERNS}.md`, `docs/CI_PLAN.md` —
  планирующие/трекинговые документы и чек-листы, отработавшие своё после завершения всех фаз;
  фактическое содержание либо перенесено в постоянные документы (`README.md`,
  `docs/bootloader/HAB_GUIDE.md`, `docs/CI_WORKFLOW.md`), либо остаётся доступным в git-истории. Тот
  же паттерн, что уже применялся к `FIRST_RELEASE_PLAN.md`/`RELEASE_ROADMAP.md`/`just/ci_workflow.md`
  при предыдущем релизе (см. запись ниже).

## [2026-07-07 .. 2026-07-13] — Первый релиз: `tui-v0.2.0`/`firmware-v0.1.2`, точечный `tui-v0.2.1`

Диапазон: `8869b3c8..d9fb813b`
Сравнение: <https://github.com/OSabuser/tft_manufacture_test/compare/8869b3c...d9fb813>

### Кратко

- Первый тег-релиз проекта: `firmware-v0.1.2` (firmware_test HAB Debug) и `tui-v0.2.0` (service-tui
  PyInstaller-бандл, macOS + Windows) — итог ветки `feature-tui-monolith` (см. запись ниже, закрыта
  этим же релизом).
- Директория service-tui переименована `tools/production/` → `tools/service_tui/`.
- Точечный релиз `tui-v0.2.1` (отдельная ветка `service-tui-fixes`) — найден и исправлен полевой баг
  записи во Flash, воспроизводившийся на случайном подмножестве плат.

### Изменено

- `tools/production/` → `tools/service_tui/` (директория и все внутренние пути/ссылки).
- Из репозитория убран ранее случайно закоммиченный `dist/` (собранные PyInstaller-бандлы) —
  добавлен `.gitignore`.

### Исправлено

- **QE-бит (Winbond) не выставлялся при auto-config Flashloader — ~50/500 плат в поле падали на
  ЛЮБОЙ flash-операции.** Option word `0xC0000007` (со старта проекта, унаследован
  `flash_backend.py`/`flash_usb.py`) не включает Quad Enable; часть партий W25Q128 приходит с завода
  с QE=0, из-за чего чип остаётся в SPI-режиме при LUT, настроенных на quad-команды →
  `status 20106 FlexSPINOR: Command Failure` на любой команде. Две промежуточные гипотезы (порядок
  commit-FCB/erase; маргинальный электрический контакт) проверены на живом железе и опровергнуты.
  Причина найдена пересчётом (не «на глаз») десятичного option word из логов NXP MCUBootUtility:
  `0xC0000207`. QE энергонезависимый — после одной корректной установки (в т.ч. случайно, через
  сторонний инструмент) плата «чинится» навсегда, что и маскировало баг как нестабильный.
- M5StampPLC: два раунда фиксов детекта порта и CAN-обмена в `service-tui`.

### Удалено

- Планирующие документы, отработавшие своё к моменту релиза — `FIRST_RELEASE_PLAN.md`,
  `RELEASE_ROADMAP.md`, `just/ci_workflow.md`, `tools/production/docs/MONOLITH_APP_PLAN.md`.
  Содержание перенесено в постоянные `README.md`/`docs/DEV_ARCH.md` (тогда ещё под именем
  `tools/production/`).

## [2026-07-07] — service-tui: монолитный spsdk-бэкенд, устойчивость к обрыву USB, упаковка PyInstaller

Диапазон: `1801f1beb959d610d31ee3dcd1f91046953117d4..22c40779ef0ec9911031d7a5272c4611b596d3e8` (мёрж в `dev` — `b4c664fe121226c4231675a150fba809e73b21d6`)
Сравнение: <https://github.com/OSabuser/tft_manufacture_test/compare/1801f1beb959d610d31ee3dcd1f91046953117d4...22c40779ef0ec9911031d7a5272c4611b596d3e8>

> Ветка `feature-tui-monolith` (от `dev`, поверх мержа `feature-tui-python`). Смёржено в `dev` и
> выпущено как часть первого релиза (`tui-v0.2.0`/`firmware-v0.1.2`) — см. запись выше.
> **Полностью заменяет предыдущую версию этой записи**: прошивка сторонних
> бинарников через subprocess (`tools/host/flash_usb.py --fcb-path` +
> `nxpimage`) была реализацией на момент Фазы 0/раннего мержа и с тех пор
> заменена прямыми вызовами `spsdk` Python API — ничего из старой записи
> больше не описывает текущий код.

### Кратко

- Прошивка в `service-tui` переведена с subprocess-обёртки над
  `nxpimage`/`sdphost`/`blhost` на прямые вызовы `spsdk` Python API
  (`McuBoot`/`SDP`/`HabImage`) — `app/flash_backend.py`, провалидировано
  byte-exact на живом железе (macOS + Windows). `tools/host/flash_usb.py`
  остаётся отдельным dev-CLI для `just host::flash*`, TUI его больше не
  вызывает ни субпроцессом, ни как библиотеку.
- Обрыв USB во время прошивки/chip erase теперь надёжно типизируется во
  всех трёх наблюдавшихся на железе сценариях (`SPSDKConnectionError`,
  `SPSDKTimeoutError`, `False`-по-таймауту без исключения) и даёт оператору
  единое понятное сообщение вместо «Непредвиденная ошибка».
- Собран первый standalone-бандл (PyInstaller, onedir) — alpha, вручную
  протестирован на macOS и Windows.
- Документация (`tools/production/README.md`+`docs/DEV_ARCH.md`, корневые
  `docs/*`, все `bsp/*/README.md`, корневой `README.md`) синхронизирована
  с фактическим состоянием кода после всех фаз миграции.

### Добавлено

- `tools/production/app/flash_backend.py` — синхронное ядро прошивки на
  spsdk: `detect_sdp`/`detect_cdc`, `load_flashloader`, `flash`,
  `erase_chip`, `build_custom_hab` (`HabImage` вместо `nxpimage` CLI),
  `write_fcb_explicit`/`write_fcb_auto`. Zero Textual/asyncio импортов,
  тестируется без event loop.
- `tools/production/app/usb_ports.py` — `resolve_serial_port()` по VID:PID
  (имя порта не переносимо между перевтыкиваниями).
- Иерархия `FlashBackendError`/`ConnectionLostError`/`DeviceNotFoundError`/
  `FlashLoaderTimeoutError`/`HabBuildError` с полем `connection_lost` —
  различает физический обрыв USB от логической ошибки прошивки без
  парсинга текста сообщения.
- `tools/production/tests/test_flash_backend.py` — вырос до 45 unit-тестов
  backend'а, включая обе ветки обрыва USB (`SPSDKTimeoutError`,
  `False`-по-таймауту + вариант B через `detect_sdp()`) и golden-тест
  byte-exact сборки HAB.
- Кнопка «✕ Выйти из приложения» на `WaitingScreen`.
- `tools/production/service_tui.spec` — PyInstaller spec (onedir).
- `tools/production/docs/RELEASE_ROADMAP.md` — дорожная карта Фаз
  4a→4b→5→6 с принятыми решениями (Р10–Р12) и статусом гейтов.

### Изменено

- `tools/production/app/flasher.py` — переведён с subprocess
  (`flash_usb.py` через `uv run`) на `asyncio.to_thread`-обёртку над
  `flash_backend.py`; сборка кастомного HAB — через `HabImage` в отдельном
  потоке, а не subprocess `nxpimage`.
- `tools/production/app/main.py` — логирование: root по умолчанию `INFO`
  (было `DEBUG`), `spsdk`/`libusbsio` принудительно приглушены до
  `WARNING` независимо от root; полный DEBUG — через
  `SERVICE_LOG_LEVEL=DEBUG`.
- `tools/production/app/screens/flash.py` — троттлинг записи в
  `#flash-log` для фазы `write` (раз на 10%, ~10 строк вместо ~135) без
  потери плавности прогресс-бара.
- `bsp/sd/src/sd.c` — `bsp_sd_init()`/`bsp_sd_deinit()` теперь делают
  аппаратный `USDHC_Reset()` + полный `memset(&g_sd, ...)` перед
  повторной инициализацией: без этого non-blocking host driver SDK мог
  оставаться в состоянии ожидания транзакции от предыдущей
  diagnostic-сессии, и следующий `f_mount()` в тесте `usd` блокировался
  навсегда.

### Исправлено

- Обёртка обрыва USB расширена с `SPSDKConnectionError` на
  `(SPSDKConnectionError, SPSDKTimeoutError)` — второй тип не наследует
  первый, но реально прилетает на read-фазе после write.
- Вариант B для команд, возвращающих `False` без исключения
  (`flash_erase_all`/`flash_erase_region`/`write_memory`): при `False`
  выполняется быстрый `detect_sdp()` — устройство пропало с шины →
  `ConnectionLostError`, устройство на месте → обычная `FlashBackendError`.
- Баг «File not found» для bootloader/app/firmware_test при резолве путей
  прошивки (Фаза 4a).
- Unit-тест моки (`test_cli.c`, `test_bsp_can.c`, `test_firmware_runner.c`,
  stub-хедеры `fsl_clock.h`/`version.h`) — фиксы после рефакторинга
  `cli.c`/`test_runner.c`.

### Тесты

- `test_flash_backend.py` — вырос до 45 тестов, включая гейт по
  `SPSDKTimeoutError` и переклассификации erase-таймаута (вариант B).

### Документация

- `tools/production/README.md`/`tools/production/docs/DEV_ARCH.md` —
  полностью пересмотрены под факт: убраны все следы subprocess/`nxpimage`/
  `flash_usb.py` из описания архитектуры прошивки; добавлены §6.2
  (обработка обрыва USB), §14 (PyInstaller/frozen-резолв путей), §15
  (логирование); зафиксирован разрыв между закоммиченным
  `service_tui.spec` (`datas` только `../shared`) и фактическим
  содержимым уже собранных релизных бандлов в `dist/`.
- `docs/testing/PROTOCOL.md` — версия `0.1.0`→`0.1.2`, добавлена команда
  `get_version` и события `test_list`/`uid_response`/`version_response`,
  матрица тестов исправлена (убраны никогда не существовавшие `uart_ttl`/
  `uart_iso`, добавлен реальный `mqs`), поток Display дополнен шагами
  ротации (`display_rot0`/`display_rot_base`).
- `docs/testing/host/HOST_CREATE_TEST.md` — был байт-в-байт дубликатом
  `docs/HOW_TO_DEBUG.md` (копипаст-баг, минимум с 2026-06-23); переписан
  как реальный гайд по добавлению host-теста.
- `docs/HOW_TO_FLASH.md` (§1.5 под факт spsdk-конвейера), `docs/DEV_ARCH.md`
  (в дереве `tools/hil/` недоставало `04_test_button.py`),
  `docs/testing/hil/HIL_CREATE_TEST.md` (пример `loaded_<n>` без `m5`
  вводил в заблуждение — питание таргета всегда идёт через M5, не только
  сигнальные реле) — актуализированы.
- `bsp/usb_cdc/README.md` (VID/PID был заявлен как заглушка `0x1234:0x0001`,
  реально прошит `0x1996:0x00AD`), `bsp/uart_host/README.md` (в списке API
  отсутствовали реальные `bsp_uart_host_deinit/rx_available/rx_flush`),
  `bsp/can/README.md` (несуществующие в коде `bsp_can.c`/`can_mock.h`/
  `bsp_can_rx_cb_t`) — исправлены по сверке с заголовками.
- `bsp/mqs/{mqs.c,mqs.h,mqs_amp.c}` — докстринги приведены в соответствие
  с кодом (были «SAI1»/«16 кГц», реально SAI3/12 кГц — подтверждено
  сверкой с `bsp/generated/clock_config.c`); `bsp/provisioning/provisioning.h`
  — докстринг порядка байт UID исправлен на соответствующий реализации
  (`provisioning.c` пишет CFG0 первым, докстринг утверждал обратное).
- Корневой `README.md` — `firmware/bootloader/`/`firmware/tft_app/`
  помечены как запланированные, а не готовые (директорий не существует,
  `add_subdirectory()` закомментирован в корневом `CMakeLists.txt`);
  добавлен ранее отсутствовавший раздел «Инструменты (`tools/`)» —
  `tools/production/` (service-tui) нигде не упоминался.

### Известные ограничения

- Auto-config Flashloader для W25Q256/512 не проверялся напрямую — решили
  не полагаться на него вообще, FCB для кастомных бинарей всегда пишется
  явно.
- Массовое программирование (авто-прошивка по факту детекта SDP, без
  подтверждения оператора) рассмотрено и отклонено — в SDP/Flashloader-режиме
  нет способа прочитать UID платы для идентификации.

## [2026-06-29] — Этапы 6г–7: MQS, HIL pytest firmware_test, Provisioning

### Кратко

- `firmware_test` получил MQS audio-тест, полный HIL pytest-стек для firmware_test
  (opto + CAN через CDC) и команду `get_uid` для чтения OCOTP UID.
- Завершён Этап 6 целиком. Этап 7 (Provisioning draft) реализован.

### Добавлено

- `bsp/mqs/` + `bsp_mqs` — SAI3 + eDMA + MQS периферия, управление громкостью
  через PWM4 SM0 + LM4875M. API: `bsp_mqs_init/deinit`, `bsp_mqs_play`,
  `bsp_mqs_is_busy`, `bsp_mqs_amp_init/deinit`, `bsp_mqs_amp_set_volume`.
- `firmware/test/src/tests/test_mqs.c` — интерактивный тест: мелодия ~4 с
  (A4 + E5, целочисленная LUT-синусоида), async-воспроизведение с USB keepalive,
  `confirm_request("mqs_tone")` → PASS/FAIL оператором.
- `tools/hil/conftest.py` — класс `FirmwareCdcClient` и фикстура `firmware_cdc`:
  подключение к firmware_test по USB CDC ACM, проверка живости через `ping→pong`.
- `tools/hil/06_test_firmware_opto.py` — HIL pytest для opto-теста через CDC:
  автоматический оркестратор M5 реле → confirm, без участия оператора.
- `tools/hil/06_test_firmware_can.py` — HIL pytest для CAN-теста через CDC:
  RX (M5→таргет) и TX (таргет→M5) направления независимо.
- `bsp/provisioning/` + `bsp_provisioning` — чтение OCOTP UID (8 байт)
  через прямой доступ к `OCOTP->CFG0/CFG1` (паттерн `fsl_silicon_id_soc.c`).
- Команда `get_uid` в протоколе v2: `{"type":"cmd","cmd":"get_uid"}` →
  `{"type":"uid_response","uid":"<16 hex символов>"}`.

### Изменено

- `protocol.h/c` — добавлена `protocol_send_uid_response()`.
- `cli.c` — добавлена ветка `strcmp(cmd_name, "get_uid")` в `handle_cmd()`;
  исправлен порядок: `strstr` заменён на `strcmp` по аналогии с остальными командами.
- `bsp/README.md` — добавлены `bsp_mqs` и `bsp_provisioning` в таблицу и дерево.

### Исправлено

- MQS: отсутствие `.pwmchannelenable = true` в `pwm_signal_param_t` (NXP SDK ≥ 2.13)
  приводило к тому что `PWM_SetupPwm()` не выставлял `OUTEN` → ШИМ не выходил на пин.
  Маскировалось отладчиком (оставлял `OUTEN` от прошлой сессии), воспроизводилось
  только при cold reset.
- OCOTP: `OCOTP_Init()` вызывал зависание при чтении UID (контроллер занят после
  USB CDC init). Исправлено переходом на прямое чтение `OCOTP->CFG0/CFG1`
  без инициализации контроллера — shadow registers доступны сразу после сброса.

### Тесты

- `06_test_firmware_opto.py` hardware-verified: тайминги `RELAY_ON_S=0.15`,
  `RELAY_OFF_S=0.5`; финальное чтение через `bsp_opto_force_read()` обходит
  race condition чётного числа ISR при дребезге реле.
- `06_test_firmware_can.py` hardware-verified: два направления (RX + TX),
  `disableSelfReception=true` на таргете.
- `test_mqs` hardware-verified: async-воспроизведение + USB keepalive работает
  корректно; blocking-вариант голодал USB за ~4 с.

### Документация

- `bsp/provisioning/README.md` — новый компонент: API, аппаратура, особенности
  прямого чтения OCOTP.
- `PROTOCOL.md` — добавлена команда `get_uid` / событие `uid_response`.

## [2026-06-23] — Buttons/display test modules и полный рефакторинг документации

Диапазон: `22f98c311a4564d8f18ed72d11635bf908046b1e..<NEW_SHA>`  
Сравнение: <https://github.com/OSabuser/tft_manufacture_test/compare/22f98c311a4564d8f18ed72d11635bf908046b1e>...<NEW_SHA>

### Кратко

- Manufacturing-test firmware получил два новых test module (`test_buttons`, `test_display`), оба hardware-verified на таргете.
- Вся проектная документация прошла полный рефакторинг: единый шаблон для BSP README, Mermaid-диаграммы вместо ASCII, структурные README для `utils/` и `port/`.

### Добавлено

- `firmware/test/src/tests/test_buttons.c` — интерактивный тест двух тактовых кнопок (GPIO_B1_14/GPIO2[30], GPIO_B1_15/GPIO2[31]); non-blocking polling 5 мс, таймаут даёт `TEST_STATUS_SKIP`; `bsp_button_init()` вызывается внутри init-фазы модуля.
- `firmware/test/src/tests/test_display.c` — интерактивный тест дисплея (два этапа: заливка цветом R/G/B/W с confirm-запросами, проверка ротации LR/UD через `bsp_display_set_rotation()`).
- `port/fatfs/README.md` — новый документ, описывает INTERFACE-архитектуру `port_fatfs_sd` и причину per-binary компиляции `diskio_sd.c`.
- `utils/prio_queue/README.md` — новый документ (модуль существовал, документация отсутствовала).

### Изменено

- Все 11 BSP README (`led`, `tick`, `uart_host`, `opto`, `can`, `button`, `usb_cdc`, `sdram`, `qspi_flash`, `sd`, `display`) приведены к единому шаблону: Аппаратура (с номерами корпуса из pin_mux) → Архитектура → API → Быстрый старт → Тестирование → Интеграция → CMake.
- `bsp/README.md` — исправлена таблица компонентов (ранее отсутствовали 6 из 11 модулей), ASCII-диаграмма концепции заменена Mermaid.
- `docs/DEV_ARCH.md` — шесть ASCII-диаграмм заменены Mermaid (`graph TB`, `flowchart`, `sequenceDiagram`); §2 разбит на две отдельные диаграммы (физические связи + состав инструментов).
- `docs/HOW_TO_FLASH.md` — ASCII bash-поток USB SDP заменён Mermaid `flowchart TD`.
- `docs/HOW_TO_DEBUG.md` — ASCII топология хост/devcontainer заменена Mermaid `flowchart LR`.
- `docs/testing/PROTOCOL.md` — четыре ASCII-блока (стенд, жизненный цикл, три интерактивных теста, state machine) заменены Mermaid (`flowchart`, `sequenceDiagram`, `stateDiagram-v2`).
- `docs/testing/host/HOST_CREATE_TEST.md` — ASCII стек заменён Mermaid.
- `docs/testing/hil/HIL_CREATE_TEST.md` — ASCII стек и цепочка фикстур заменены Mermaid.
- `README.md` (корневой) — убрана устаревшая таблица BSP с 5 из 11 модулями, заменена ссылкой на `bsp/README.md`; исправлено описание `tft_app`.
- `utils/README.md` — добавлен пропущенный модуль `prio_queue`, добавлен раздел CMake.
- `port/README.md` — переработан с Mermaid-диаграммой архитектуры слоёв.
- `port/log/README.md` — добавлена Mermaid-диаграмма потока, раздел FreeRTOS с реальным кодом.

### Тесты

- `test_buttons` hardware-verified: обнаружен и исправлен баг `cli.c` — `g_s_line_len` должен сбрасываться в `0` до вызова `process_line()`, иначе входящие байты корруптят буфер команды в blocking confirm-wait цикле.
- `test_display` hardware-verified: уточнена семантика ротации — `SHLR` управляет горизонтальным направлением (LR пин), `UPDN` — вертикальным (UD пин); enum использует семантические имена (`BSP_DISPLAY_ROTATE_0`, `BSP_DISPLAY_FLIP_VERTICAL` и др.).

### Отфильтрованный шум

- Нормализация форматирования внутри уже существующих документов без изменения содержания.
- Перестановки в порядке разделов там, где смысл не менялся.

## Базовый срез — 2026-05-13

Диапазон: вся история репозитория от initial commit до `22f98c311a4564d8f18ed72d11635bf908046b1e`

### Кратко

- Репозиторий представляет собой firmware-монорепозиторий для платы на базе NXP MIMXRT1052CVJ5B. Он охватывает manufacturing-test firmware, планируемые bootloader/application firmware, BSP-модули, host tooling, HIL tooling и документацию.
- К базовому срезу в проекте уже есть полноценный BSP-слой, двухуровневая стратегия тестирования, USB-CDC архитектура тестовой прошивки, GitHub Actions CI и обширная инженерная документация.

### Текущая архитектура

- BSP-модули на момент базового среза: `led`, `tick`, `uart_host`, `opto`, `can`, `button`, `usb_cdc`, `sdram`, `qspi_flash`, `sd`, `display`.
- Host-тесты используют Unity/fff-подобные моки и инструменты, рассчитанные на devcontainer.
- HIL-тесты используют pyOCD, pyserial, pytest и поддержку M5StampPLC.
- Командный путь firmware-test состоит из USB-CDC ACM CLI, бинарного протокола, `test_runner` и отдельных firmware-side test modules.
- Документация покрывает архитектуру разработки, прошивку, отладку, HAB, HIL, host-тесты и поведение протокола.

### Состояние CI

- GitHub Actions уже есть и собирает проект внутри devcontainer.
- Видимый пробел: host unit tests и HIL tests пока не входят в GitHub Actions workflow.

## [2026-05-08] — Display test module и выравнивание документации

### Кратко

- Базовый срез заканчивается тем, что поддержка display становится частью и BSP, и manufacturing-test firmware.
- README и инженерные документы были синхронизированы между несколькими модулями, поэтому это documentation-heavy, но смысловое изменение состояния проекта.

### Добавлено

- Реализован `bsp/display` с API, исходниками и интеграцией в сборку.
- Добавлен `firmware/test/src/tests/test_display.c` как firmware-side test module для display.

### Изменено

- README нескольких BSP-модулей и manufacturing-test firmware приведены к более единому стилю.
- `docs/DEV_ARCH.md`, документы по прошивке/отладке и HIL-гайды обновлены под актуальную структуру проекта.
- `just/ci_workflow.md` расширен дополнительными деталями CI workflow.

### Документация

- Документация по display, SD, CAN, button, USB-CDC и firmware-test получила содержательные обновления, а не только форматирование.

### Удалено

- Удалён `bsp/qspi_flash/REFACTORING.md`, так как временный план потерял актуальность.

### Отфильтрованный шум

- Чистая нормализация стиля в документации не учитывалась как отдельная feature, если она не меняла содержание или проектные инструкции.

## [2026-05-07] — USD/SD test flow

### Кратко

- SD/MMC testing перешёл от BSP/middleware-работ к firmware-test module с обновлением протокола и тестовой документации.

### Добавлено

- Добавлен `firmware/test/src/tests/test_usd.c`, затем доведён до usable SD/MMC test module.

### Изменено

- `firmware/test/src/main.c` упрощён по мере модульного оформления test modules.
- `docs/testing/PROTOCOL.md`, `firmware/test/src/tests/README.md` и `firmware/test/PLAN.md` обновлены под USD/SD test coverage.

### Тесты

- Firmware-side SD/MMC testing стал видимым отдельным модулем, хотя базовый срез всё ещё требует отслеживать стабильное HIL-покрытие SD.

## [2026-05-06] — SD card и FatFS integration

### Кратко

- Поддержка SD card и FatFS вошла в BSP и firmware-test stack.
- NXP SD middleware был пропатчен, что создаёт будущую точку контроля для vendor SDK drift.

### Добавлено

- Добавлен `bsp/sd` с API, реализацией, README и CMake-интеграцией.
- Добавлены `firmware/test/fatfs` и `port/fatfs/sd` для подключения FatFS к SD BSP.
- Добавлены generated SDMMC configuration files.

### Изменено

- SDK SD middleware и SDMMC host code изменены для поддержки smoke-test path.
- Clock и pin configuration скорректированы под SD/MMC.

### Тесты

- Появился SD smoke-test, но стабильное HIL regression coverage ещё не закреплено в базовом срезе.

### Отфильтрованный шум

- `project_tree.txt` и `sdk/sdk_tree.txt` рассматривались как artifacts состояния репозитория, а не как функциональные изменения.

## [2026-04-23] — Добавлен GitHub Actions CI

### Кратко

- CI стал реальным: проект получил GitHub Actions workflow, который собирает проект внутри devcontainer.
- Workflow ориентирован на сборку; host tests и HIL tests остаются будущей работой.

### Добавлено

- Добавлен `.github/workflows/ci.yml` с triggers на push, PR и manual dispatch.
- Workflow собирает devcontainer image, запускает `just ci::build` и загружает build tree как artifact.
- Добавлен `just/ci_workflow.md` с описанием CI flow.

### Изменено

- `just/ci.just` скорректирован под CI build path.

### CI

- CI покрывает воспроизводимость containerized build.
- CI пока не запускает host unit tests.
- CI пока не запускает HIL tests, которым требуется подключённое железо или self-hosted runner.

### Удалено

- Удалён `.clang-tidy` override для generated code из `bsp/generated`.

## [2026-04-20 .. 2026-04-22] — SDRAM и QSPI firmware-test modules

### Кратко

- Manufacturing-test firmware получил memory-oriented test modules для SDRAM и QSPI Flash.
- QSPI support появился и как BSP module, и как firmware-side test.

### Добавлено

- Добавлены API и реализация `bsp/sdram`.
- Добавлен `firmware/test/src/tests/test_sdram.c`.
- Добавлен `bsp/qspi_flash` с API, реализацией и README.
- Добавлен `firmware/test/src/tests/test_qspi.c`.
- Добавлен `firmware/test/src/tests/README.md` с описанием firmware-side test modules.

### Изменено

- `firmware/test/src/main.c` обновлён для интеграции новых test modules.

### Тесты

- SDRAM и QSPI вошли в firmware-test command model.

### Документация

- Документация по test modules начала описывать растущий firmware-test suite.

## [2026-04-17] — Binary protocol, test runner и hardware documentation

### Кратко

- Manufacturing-test firmware перешёл от простого CLI к protocol-driven test runner architecture.
- Hardware reference documentation существенно расширилась.

### Добавлено

- `protocol.c/.h` ввели binary protocol для управления тестами.
- `test_module.h` и `test_runner.c/.h` ввели modular firmware-test runner.
- Добавлены host tests для protocol и runner behavior.
- `docs/testing/PROTOCOL.md` описал binary protocol.
- Добавлены hardware PDFs и board/display reference materials в документацию.

### Изменено

- `firmware/test/README.md` сильно переписан под новую архитектуру.
- Existing CLI host tests были расширены.

### Тесты

- Host coverage расширился на protocol, runner и CLI behavior.

### Удалено

- Старые planning/architecture artifacts в `firmware/test` и HIL refactor notes удалены после замены новой структурой.

### Отфильтрованный шум

- Большие добавления hardware PDF сведены по назначению, без перечисления каждого файла.

## [2026-04-07 .. 2026-04-09] — USB-CDC CLI и priority queue

### Кратко

- Test firmware получил modular USB-CDC CLI.
- Добавлен priority queue utility и host coverage для него.

### Добавлено

- `firmware/test/src/cli.c` и `cli.h` ввели отдельный CLI module.
- `tests/host/cli/test_cli.c` добавил host coverage для CLI behavior.
- Добавлен `utils/prio_queue` с README и большим host test.
- Появилась placeholder structure для `bsp/display`.

### Изменено

- Старый monolithic `firmware/test/main.c` path заменён на modular source layout.
- Добавлены firmware-test planning docs вокруг CLI/protocol roadmap.

### Тесты

- Host tests покрыли CLI и priority queue.

## [2026-04-03 .. 2026-04-06] — USB-CDC stack и HIL coverage

### Кратко

- USB-CDC ACM стал реальной BSP capability и был подключён к HIL validation.
- HIL configuration и documentation были уточнены вокруг нового USB path.

### Добавлено

- Реализован `bsp/usb_cdc` с API, descriptors, Chapter 9 handling и hardware wrappers.
- SDK USB middleware integration добавлена в сборку.
- `tests/target/hil_usb_cdc` и `tools/hil/05_test_usb_cdc.py` добавили USB-CDC HIL coverage.

### Изменено

- Обработка HIL configuration была отрефакторена, `.env.example` получил новые переменные.
- `bsp/usb_cdc/README.md` переписан с фокусом на API documentation.

### Тесты

- USB-CDC вошёл в numbered HIL suite после UART, opto, CAN и button.

### Документация

- HIL docs, включая bench, creation guide, fixtures и how-to, были обновлены.

## [2026-04-01 .. 2026-04-02] — Clock, pin и MPU setup

### Кратко

- Board generated files стали полнее: в firmware base вошли clock, pin и MPU configuration.

### Добавлено

- Добавлены generated clock и pin-mux configuration files.
- Добавлен `TFT_Board.mex` как project state NXP Config Tools.
- Появились initial empty `bsp/sdram` placeholders.

### Изменено

- Board startup/configuration получил MPU initialization.
- Linker scripts скорректированы для FlexSPI NOR и RAM layout.
- `firmware/test/main.c` упрощён вокруг нового initialization path.

## [2026-03-30 .. 2026-03-31] — CAN и button BSP с host/HIL tests

### Кратко

- CAN и button support стали полноценными BSP modules с host и HIL validation.
- HIL tests были пронумерованы в ordered suite.

### Добавлено

- Добавлен `bsp/can` с API, реализацией, README и mocks.
- Добавлены host tests и HIL target/test code для CAN.
- Добавлен `bsp/button` с API, реализацией и README.
- Добавлены host tests и HIL target/test code для button.

### Изменено

- HIL pytest files переименованы в ordered sequence: UART, opto, CAN, button.
- HIL documentation обновлена по мере конкретизации test suite.

### Тесты

- CAN и button получили host test coverage.
- CAN и button получили HIL coverage.

### Удалено

- Удалён `bsp/can/PLAN.md` после переноса полезного содержания в README.

## [2026-03-26 .. 2026-03-28] — M5StampPLC HIL bench и fixture documentation

### Кратко

- HIL стал больше чем pyOCD prototype: появились M5StampPLC support, power/control helpers и fixture documentation.

### Добавлено

- Добавлена поддержка M5StampPLC в `tools/hil`, включая agent/CLI logic и MicroPython firmware assets.
- HIL support code реорганизован в M5-specific helpers.
- `tests/target/hil_opto` и `tools/hil/test_opto.py` добавили opto HIL coverage.
- `docs/testing/hil/HIL_FIXTURES.md` описал pytest fixtures для HIL.

### Изменено

- `just/host.just` и `tools/hil/conftest.py` существенно расширены под HIL workflows.
- Документация реорганизована в более понятные иерархии hardware, MIMXRT1052 и testing.

### Тесты

- Opto inputs получили HIL-level validation.

### Удалено

- Старые locations CMake/test guides заменены новой иерархией `docs/testing`.

### Отфильтрованный шум

- Перемещения PDF и документов учтены как изменение структуры документации, а не как отдельное content change для каждого файла.

## [2026-03-23] — Logging infrastructure и BSP opto

### Кратко

- Появились logging infrastructure и opto input BSP вместе с host coverage.

### Добавлено

- `port/log` и `utils/log` ввели logging abstractions и UART-oriented logging support.
- Добавлены host tests для logging behavior.
- Добавлен `bsp/opto` с API, реализацией, README, GPIO mocks и host tests.

### Тесты

- Host coverage расширился на logging и opto behavior.

### Удалено

- Корневой `TODO.md` удалён после переноса планирования в другие места.

## [2026-03-18 .. 2026-03-20] — Первый HIL skeleton и flashing/debug docs

### Кратко

- Проект получил первый HIL skeleton и target-side UART validation path.
- Flashing и debugging tooling стали документированными и scriptable.

### Добавлено

- Добавлен `tools/hil` с pytest/pyOCD/pyserial-oriented utilities.
- `tests/target/host_uart` предоставил target firmware для UART HIL validation.
- `tools/host/flash_swd.py` добавил SWD flashing support.
- Добавлены `docs/HOW_TO_DEBUG.md`, расширенные flash docs и tool README.

### Изменено

- Main README и development architecture docs расширены вокруг host/container workflow и testing.
- Добавлены host и HIL test creation guides.

### Тесты

- Появился первый UART HIL path.

### Удалено

- Temporary flash logs удалены после окончания диагностической пользы.

## [2026-03-16 .. 2026-03-17] — Первые BSP modules и host test infrastructure

### Кратко

- Репозиторий получил первые concrete BSP modules и host-test layout.

### Добавлено

- Добавлены `bsp_led` и `bsp_tick` с API, реализацией и README files.
- `bsp/uart_host` добавил LPUART1/MCU-Link VCOM support с API, реализацией, README, mocks и host tests.
- Введены shared BSP status codes.
- Добавлен `utils/ring_buffer`.
- Добавлен `tests/host` с mocks и tests для LED, ring buffer и timeout behavior.
- `TODO-HIL.md` зафиксировал initial HIL plan.

### Тесты

- Host testing начался с Unity/fff-style mocks и isolated test directories.

## [2026-03-13 .. 2026-03-15] — Draft архитектуры manufacturing-test firmware

### Кратко

- Архитектура manufacturing-test firmware была задокументирована до последующей реализации protocol/runner.

### Добавлено

- `firmware/test/README.md` и `firmware/test/arch.svg` описали первый architecture concept.
- BSP и USB-CDC README зафиксировали early design intent.

### Изменено

- `bootstrap.sh` был упрощён.

### Удалено

- Ранняя VS Code launch configuration удалена при cleanup bootstrap.

## [2026-03-10 .. 2026-03-12] — Project environment, BSP base и HAB flow

### Кратко

- Проект перешёл от пустого scaffold к buildable embedded workspace с generated board support, HAB assets и containerized tooling.

### Добавлено

- Добавлен board support для MIMXRT1052: startup code, generated config, linker scripts и FlexSPI NOR-related assets.
- Добавлен HAB signing/configuration flow для app, bootloader и firmware-test images.
- Добавлены formatting/lint/editor configuration.
- Добавлены devcontainer, Dockerfile, CMake presets, VS Code tasks и bootstrap scripts.
- `Justfile` стал entry point для build и host automation.
- Добавлена development architecture и CMake hints documentation.

### Изменено

- Generated NXP Config Tools content перенесён из `bsp/board` в `bsp/generated`.
- Логика `Justfile` разделена на modules под `just/`.
- Добавлена flashing documentation, bootstrap logic переработана.

### Документация

- Early docs явно отмечали, что CI и tests ещё не покрыты.

## [2026-03-05] — Initial repository scaffold

### Кратко

- Репозиторий инициализирован с базовой metadata и README placeholder.

### Добавлено

- Добавлены `.gitattributes`, `.gitignore` и начальный `README.md`.

## Отфильтрованный шум базового среза

Следующие классы изменений были просмотрены, но намеренно не развёрнуты в подробные записи changelog:

- Merge commits без смысловых content changes.
- Дублирующиеся или неинформативные commit subjects, где значимость определялась по diff, а не по title.
- Чистое форматирование или переносы строк в документации.
- Перенумерация HIL tests, когда поведение не менялось.
- Bulk vendor SDK imports и generated code churn, кроме случаев, где они влияли на локальные patches или build effects.
- Перемещения PDF и datasheets, кроме случаев изменения структуры документации.

## Контракт еженедельного мониторинга

Регулярная еженедельная проверка должна:

- Сравнивать предыдущий сохранённый SHA с текущим HEAD ветки `dev`.
- Обновлять блок `[Не выпущено]` или создавать новый датированный блок наверху при наличии смысловых изменений.
- Предпочитать строгие русскоязычные категории из этого файла.
- Упоминать изменения README только тогда, когда они влияют на onboarding, понимание API, инструкции сборки/тестирования или архитектуру проекта.
- Упоминать изменения тестовой инфраструктуры, когда они затрагивают host tests, HIL tests, firmware-side test modules, fixtures, runners, protocol commands или expected coverage.
- Упоминать CI changes, когда меняются workflows, local CI tasks, artifacts, triggers, status или self-hosted runner strategy.
- Молчать, если новых коммитов нет или найден только отфильтрованный шум.
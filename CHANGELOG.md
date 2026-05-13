# Журнал изменений: `tft_manufacture_test`

Репозиторий: https://github.com/OSabuser/tft_manufacture_test.git  
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

Диапазон: после `22f98c311a4564d8f18ed72d11635bf908046b1e`

### Кратко
- После базового среза новые изменения ещё не обрабатывались.

### Что отслеживать
- Проверять, остаётся ли `.github/workflows/ci.yml` только сборочным workflow через `just ci::build` или начинает запускать host-тесты.
- Отслеживать появление self-hosted runner, регулярных аппаратных прогонов или отчётов по HIL.
- Проверять новые firmware-test модули в `firmware/test/src/tests/` и синхронные обновления протокола/документации.
- Следить за развитием BSP: MQS, RGB, bootloader или `tft_app`.
- Отслеживать локальные патчи поверх vendor SDK, которые нужно вести отдельным patch log.

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

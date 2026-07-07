# План первого релиза в GitHub

> Цель: в GitHub Releases появляется тег с прикреплёнными бинарниками —
> `firmware_test` (HAB-образ) и `service-tui` (standalone-бандлы под macOS и
> Windows). Этот документ — практический чек-лист «как довести до кнопки
> Publish», а не повторение инженерных фаз `RELEASE_ROADMAP.md`.

## Как связаны два артефакта

`firmware_test` (C, i.MX RT1052, версия `0.1.2` из
`firmware/test/CMakeLists.txt`) и `service-tui` (Python/Textual, версия
`0.2.0` из `tools/service_tui/pyproject.toml`) — независимо версионируемые
проекты, но релиз одного без другого бесполезен сервисному инженеру:
`service-tui` — это инструмент, которым он *прошивает* плату диагностической
прошивкой, и HAB-образ `firmware_test` кладётся внутрь бандла TUI как
`firmware/<Type>/firmware_test_hab.bin` (см. `just host::package-tui`,
`tools/service_tui/docs/DEV_ARCH.md` §14). Поэтому релиз собирается как один
комплект, даже если версии независимые.

Известное ограничение (задокументировано в `README.md`/`DEV_ARCH.md`):
Release-сборка `firmware_test` нестабильна (проблема с FCB/clock), поэтому
в бандл TUI кладётся **Debug**-образ (`FIRMWARE_BUILD_TYPE=Debug`). В релиз
GitHub имеет смысл положить оба HAB-образа отдельными assets (Debug — как
основной для TUI, Release — с пометкой «experimental», для тех, кто прошивает
через `tools/host/flash_usb.py` вручную), либо только Debug — см. открытый
вопрос в шаге 1.5.

---

## Текущее состояние (снимок на момент написания плана)

| Область | Состояние |
| --- | --- |
| `firmware_test` | Собирается, HAB-образ генерируется (`just build::hab-firmware-test-{debug,release}`), Release нестабилен |
| `service-tui` | v0.2.0, PyInstaller onedir, alpha-бандлы уже вручную собраны и прогнаны на живом железе macOS+Windows (коммиты `c694258`/`bfe4dd6`) |
| `service_tui.spec` | **Устарел относительно того, чем реально собраны протестированные alpha-бандлы** — не содержит `datas` для `spsdk`, `dcd/*.bin`, `pyproject.toml` (задокументировано в `DEV_ARCH.md` §14 и `CHANGELOG.md`「Известные ограничения」) |
| `tools/service_tui/dist/service-tui-v0.2.0-{macos,windows}/` | Закоммичены в git (906 файлов, ~96 МБ суммарно) и **устарели относительно HEAD** — собраны до коммитов `2dbe3e6`/`22c4077`/`31e3237` (фиксы моков тестов, рефакторинг докстрингов) |
| CI (`.github/workflows/ci.yml`) | Только `build`+`test` в devcontainer на `ubuntu-latest`; не собирает `service-tui`, нет macOS/Windows раннеров, нет release-пайплайна, нет тегов в репозитории |
| `just/ci.just` | Есть рецепт `release` (→ `just build::hab-all-release`) — только firmware, ничего про упаковку TUI или публикацию на GitHub |
| Ветки | `feature-tui-monolith` на 15 коммитов впереди `dev`, ещё не смёржена; в репозитории также есть `main` — политика, какая ветка режет релизы, явно не зафиксирована |

---

## Шаг 1 — Закрыть блокирующие долги перед тегом

Без этого CI-сборка (шаг 3) не будет соответствовать тому, что уже
провалидировано на железе, — а «релиз, который не воспроизводим из
исходников» хуже отсутствия релиза.

1. **Актуализировать `tools/service_tui/service_tui.spec`** — добавить
   `collect_data_files("spsdk")` (+ `SPSDK_DATA_FOLDER` если понадобится),
   `collect_dynamic_libs("libusbsio")`, `datas` для
   `tools/host/dcd/{dcd.bin,w25q128_fdcb.bin,w25q512_fdcb.bin,ivt_flashloader.bin}`
   и `pyproject.toml`. Ориентир — реальное содержимое уже собранных
   alpha-бандлов в `dist/` (их можно инспектировать перед удалением из git,
   см. следующий пункт).
2. **Убрать `tools/service_tui/dist/` из git**: `git rm -r --cached
   tools/service_tui/dist` + добавить `tools/service_tui/dist/` в
   `.gitignore`. Собранные бандлы — это build-артефакты, их место в GitHub
   Release assets или CI-артефактах, не в истории репозитория.
3. **(Дёшево, но не блокирует)** Убрать мёртвую зависимость `pyusb` из
   `tools/service_tui/pyproject.toml` — детект давно переведён на
   `spsdk`/`serial.tools.list_ports` (Р7), ни один модуль `app/` её не
   импортирует.
4. **Пересобрать бандлы локально** с исправленным spec из актуального HEAD
   (`just host::package-tui` на macOS и на Windows) и повторить хотя бы
   дымовой прогон чек-листа Гейта 5 из `RELEASE_ROADMAP.md` (детект SDP →
   прошивка `firmware_test` → диагностика → выход). Полный деструктивный
   чек-лист (обрыв USB и т.п.) уже пройден на предыдущей сборке — здесь
   цель убедиться, что исправленный spec не сломал состав бандла, а не
   повторять всё с нуля.
5. **Открытый вопрос:** класть ли в релиз Release-сборку `firmware_test`
   вообще (сейчас нестабильна) — по умолчанию план предполагает **только
   Debug**-образ как основной asset, Release не публикуется до починки
   FCB/clock-проблемы. Требует подтверждения.

   → **Решено:** только Debug.
6. **(Добавилось по ходу, не было в исходном плане)** Помимо самого
   `datas`/`binaries` в spec, ужесточили сборку двумя хардфейлами вместо
   тихих warning'ов:
   - `service_tui.spec` теперь падает с `FileNotFoundError`, если в
     `tools/host/dcd/` нет хотя бы одного из
     `dcd.bin`/`ivt_flashloader.bin`/`w25q128_fdcb.bin`/`w25q512_fdcb.bin`;
   - `just host::package-tui` падает с `exit 1`, если не нашёлся Debug
     `*_hab.bin` в `build/Debug` (Release остаётся необязательным).
   Заодно поправлен баг расположения `custom_binaries/` — recipe создавал
   пустую декоративную папку рядом с `service_tui/`, а не внутри неё, хотя
   `flasher._resolve_custom_binaries_dir()` смотрит именно внутрь (рядом с
   исполняемым файлом); теперь `custom_binaries/` создаётся в правильном
   месте и сразу наполняется `TFT_BOOTLOADER_NEW.bin`/`TFT_BOOTLOADER_OLD.bin`
   из `tools/service_tui/custom_binaries/`.

   **TODO (отложено, не забыть перед шагом 2):** актуализировать
   `tools/service_tui/README.md` и `tools/service_tui/docs/DEV_ARCH.md` §14 —
   они всё ещё описывают старое поведение (в частности, блок «Расхождение
   spec/факт» в DEV_ARCH.md §14 уже неактуален, spec восстановлен и
   ужесточён). Сознательно отложено до ручной валидации сборки на
   macOS/Windows (пункт 4) — чтобы задокументировать то, что реально
   проверено на железе, а не то, что должно было бы работать.

---

## Шаг 2 — Версия и тег

1. **Схема тега** — `firmware_test` (0.1.2) и `service-tui` (0.2.0)
   версионируются независимо. Предлагается: тег вида `vX.Y.Z` = версия
   `service-tui` (это главный продукт релиза для сервисного инженера),
   версия `firmware_test` указывается в описании релиза отдельной строкой.
   Альтернатива — раздельные теги (`tui-v0.2.0` + `firmware-v0.1.2`), если
   в будущем оба проекта должны релизиться независимо друг от друга.
   **Требует подтверждения**, план ниже считает первый вариант.
2. **Ветка релиза** — в репозитории есть и `dev`, и `main`, при этом
   `main` в `git log` не встречается в истории `feature-tui-monolith`/`dev`
   (нужно свериться отдельно, если `main` уже используется под что-то
   другое). Рекомендация: смёржить `feature-tui-monolith → dev`, затем
   `dev → main`, тег ставить на `main` — так `main` остаётся точкой,
   соответствующей опубликованным релизам, а `dev` — интеграционной веткой.
   **Требует подтверждения**, если у проекта другая договорённость про
   `main`.
3. **`CHANGELOG.md`** — закрыть секцию `[Не выпущено] — service-tui: ...`
   → `[YYYY-MM-DD] — v0.2.0`, вычеркнуть из «Известные ограничения» то, что
   закрывается шагом 1 (spec-расхождение, `pyusb`).

---

## Шаг 3 — CI: собрать релизные бинарники автоматически

Текущий `.github/workflows/ci.yml` собирает только `firmware_test` на
`ubuntu-latest` внутри devcontainer — этого недостаточно для
кросс-платформенной упаковки `service-tui`. Нужен отдельный workflow,
не смешанный с обычным PR-циклом (см. `just/ci_workflow.md`, «Шаг 3 —
выделить release workflow»).

Новый `.github/workflows/release.yml`, триггер — тег `v*` (плюс
`workflow_dispatch` для тестового прогона без публикации):

| Job | Раннер | Что делает |
| --- | --- | --- |
| `firmware` | `ubuntu-latest` (тот же devcontainer-подход, что в `ci.yml`) | `just ci::release` → `hab-all-release` (по факту нужен только `firmware_test`, Debug+Release); выгрузить `firmware_test_hab.bin` (оба типа) как артефакт |
| `service-tui-macos` | `macos-latest` | скачать firmware-артефакт из job `firmware`; `uv sync` в `tools/service_tui`; `just host::package-tui`; заархивировать `dist/service-tui-vX.Y.Z-macos/` |
| `service-tui-windows` | `windows-latest` | то же самое, PowerShell-совместимые команды (`just`/`uv` доступны на Windows) |
| `publish-release` | `ubuntu-latest`, `needs: [firmware, service-tui-macos, service-tui-windows]` | скачать все артефакты, создать GitHub Release через `gh release create` / `softprops/action-gh-release@v2`, прикрепить `firmware_test_hab.bin` (Debug, + Release с пометкой experimental, если решение по шагу 1.5 — «класть оба»), `service-tui-vX.Y.Z-macos.zip`, `service-tui-vX.Y.Z-windows.zip` |

Важные нюансы:

- Firmware для бандла TUI собирается **один раз** в job `firmware` и
  передаётся в macOS/Windows job'ы артефактом — пересобирать ARM-прошивку
  на каждом раннере отдельно избыточно (и на macOS/Windows раннерах нет
  подготовленного devcontainer/toolchain).
- Аппаратные гейты (детект SDP на живой плате, деструктивные сценарии
  обрыва USB) **CI выполнить не может** — GitHub-hosted раннеры не видят
  реальное USB-устройство. Это ручной шаг, который уже пройден один раз
  вручную (коммиты «MacOS tested»/«Windows tested») и должен повторяться
  вручную перед каждым релизом, пока не поднят self-hosted HIL-раннер
  (см. `just/ci_workflow.md`, «Шаг 5»). План релиза это не блокирует, но
  release notes должны явно фиксировать, что сборка прошла ручную проверку
  на железе, а не только CI.

---

## Шаг 4 — Ручные шаги перед Publish

1. Скачать `service-tui-vX.Y.Z-{macos,windows}.zip`, собранные именно CI
   (не локальную сборку из шага 1.4) — прогнать сокращённый чек-лист Гейта
   5: детект SDP → прошивка `firmware_test` → диагностика на обеих ОС.
   Цель — убедиться, что CI-сборка не разошлась с уже провалидированной
   локальной.
2. Обновить `tools/service_tui/README.md`/корневой `README.md` — ссылка на
   релиз/инструкция «откуда скачать сервисному инженеру».

## Шаг 5 — Публикация

```bash
git tag vX.Y.Z
git push origin vX.Y.Z
```

— триггерит `release.yml`. Проверить, что все 3 asset'а прикрепились и
release notes корректны (описание можно сгенерировать из секции
`CHANGELOG.md` за этот релиз + `gh release create --generate-notes` как
дополнение). Если Гейт 6 (`RELEASE_ROADMAP.md`) закрыт не полностью
(например, POST-1 сознательно отложен — это нормально, он и заявлен как
пост-релизный) — релиз всё равно можно публиковать как обычный, а не
pre-release, POST-1 не блокирует v1 по замыслу roadmap.

---

## Шаг 6 — Развитие CI после первого релиза (не блокирует, но логично заложить сразу)

Эти пункты уже зафиксированы в `just/ci_workflow.md` («Рекомендуемые
следующие шаги»), возвращаемся сюда после первого релиза:

- `lint` job (`just ci::lint` сейчас заглушка) — `clang-format --dry-run
  --Werror` + `clang-tidy`.
- Coverage (`just ci::_coverage` уже есть, но не подключён в workflow).
- Публикация devcontainer image в GHCR — сократит время `firmware` job в
  `release.yml` и обычном `ci.yml` (сейчас образ пересобирается в каждой
  job, даже с layer-кэшем).
- Self-hosted HIL lane — единственный способ когда-нибудь автоматизировать
  то, что сейчас в шаге 4 делается руками.

---

## Сводная последовательность

```
Шаг 1 (spec + dist из git + пересборка)
   │
Шаг 2 (тег/ветка/CHANGELOG — решения по открытым вопросам)
   │
Шаг 3 (release.yml: firmware → macOS/Windows package → publish)
   │
Шаг 4 (ручная проверка CI-бинарников на железе)
   │
Шаг 5 (git tag → publish)
   │
Шаг 6 (lint/coverage/GHCR/HIL — после релиза, не блокирует)
```

## Открытые вопросы, требующие решения пользователя

| # | Вопрос | Где всплывает |
| --- | --- | --- |
| 1 | Класть ли Release-сборку `firmware_test` в релиз (сейчас нестабильна) | Шаг 1.5 |
| 2 | Схема тега — один `vX.Y.Z` (=версия TUI) или раздельные теги firmware/TUI | Шаг 2.1 |
| 3 | Тег ставится на `main` (после `dev → main`) или сразу на `dev`/на самой feature-ветке | Шаг 2.2 |

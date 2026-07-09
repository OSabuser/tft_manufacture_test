# Загрузчик (firmware/bootloader) — план разработки по фазам

## Статус

| Фаза | Статус | Примечание |
|---|---|---|
| 0 — Карта Flash | ✅ завершена | [docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md](../../docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md) |
| 1 — Скелет (CDC + LED) | ✅ завершена | сборка/HAB/SWD-прошивка/ping-pong/debug — все пункты верификации пройдены на реальной плате, детали ниже |
| 2 — bootutil (Direct-XIP) | 🟡 host-тесты завершены | реальный bootutil+TinyCrypt+imgtool-фикстуры, 5/5 тестов зелёные; ARM-сторона (flash_map_backend над bsp_qspi_flash, boot_select.c/jump, сборка/железо) — впереди |
| 3 — SD-путь установки | не начата | |
| 4 — SDRAM/W25Q smoke-test + LED-паттерны | не начата | |
| 5 — HAB Release + service-tui | не начата | |

## Контекст

`service-tui` и `firmware_test` уже выпущены и работают на производстве. Следующий шаг —
`firmware/bootloader/` (сейчас пустая директория, но сборочная инфраструктура под неё уже
существует: пресеты `bootloader-debug/release` в CMakePresets.json, рецепты `just build::*`,
`.vscode/launch.json`, HAB-конвейер) и `firmware/tft_app/`. Начинаем с загрузчика — он определяет
контракт (flash-layout, формат образа, версия), под который потом пишется tft_app.

Архитектурные решения, принятые в обсуждении (не пересматриваются в рамках этого плана):

- **Boot-стратегия**: и bootloader, и tft_app исполняются XIP из W25Q. Bootloader без ITCM-копирования,
  без DCD — он не трогает SDRAM. tft_app сама поднимает SEMC в своём раннем startup (SDRAM — под её XIP,
  см. отдельный будущий план на tft_app).
- **Схема обновления**: MCUboot **Direct-XIP**, два слота (A/Б) с полностью валидными образами каждый,
  без swap/scratch. Единственный полевой канал обновления — microSD. USB как канал заливки *образа*
  сознательно не делаем (SDP/blhost на производстве — это отдельный, не зависящий от кода bootloader,
  канал через NXP BootROM).
- **Верификация образов tft_app** — через **bootutil** (MCUboot), не самодельный верификатор
  (см. исследование ниже). Подпись — `imgtool` (не HAB; HAB — только для самого bootloader).
- **Обратная связь** — USB CDC ACM с тем же JSON-lines протоколом (`ping`/`pong`, `get_version`), что уже
  использует `firmware_test` и `tools/service_tui/app/firmware_client.py` — чтобы переиспользовать
  клиентский код на стороне service-tui. Плюс `LED_HEARTBEAT`/`LED_APP` (`bsp/led`) с кодированием
  состояний через паттерн мигания.
- **Даунгрейд**: строгий version-gate по умолчанию; обход — удержание кнопки (`BSP_BUTTON_1`,
  `bsp/button`) при старте с валидным (но более старым) подписанным образом на SD.
- **Два сценария производства** обслуживаются одним и тем же механизмом установки с SD
  (bootloader-only → потом массовая SD-установка, или сразу залитый бандл) — единственное отличие:
  top-level состояние "нет ни одного валидного слота" (ожидание SD, retry-цикл, статус на CDC/LED),
  которого нет в обычной работе.
- **Smoke-test** (SDRAM + W25Q) — встроен в bootloader как опциональный, неблокирующий шаг: переиспользует
  `bsp_qspi_init()`/`bsp_qspi_read_jedec_id()` (bsp/qspi_flash, и так нужен для доступа к слотам) и
  `bsp_sdram_init()` (bsp/sdram) поверх новой общей функции подъёма SEMC, которую позже переиспользует
  и tft_app.

### Что нашли по MCUboot bootutil (исследование Explore-агента)

В `sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/` уже есть готовый порт bootutil под NXP MCUX SDK
(`flashapi/flash_api.c` — 425 строк, ровно тот шим над flash-драйвером, который нужен и нам;
`include/{sysflash,flash_map_backend,mcuboot_config}/*.h`; `boot.c`/`boot.h` — pattern main-loop
загрузчика). В `sdk/boards/evkbimxrt1050/ota_examples/ota_mcuboot_basic/armgcc/` есть рабочий линкер-скрипт
`MIMXRT1052xxxxx_flexspi_nor_mcuboot.ld` — тот же SoC, что и у нас. `bootutil_priv.h` требует ровно один
из `MCUBOOT_OVERWRITE_ONLY`/`MCUBOOT_SWAP_USING_MOVE`/`MCUBOOT_DIRECT_XIP`/`MCUBOOT_RAM_LOAD` — включение
`MCUBOOT_DIRECT_XIP` чисто вырезает всю swap/scratch-машинерию через `#if`. Итоговая оценка порт-кода:
~500-1100 строк (flash-шим над `bsp_qspi_flash`, `sysflash.h` на 2 слота, `mcuboot_config.h`,
статический `malloc`/`free`-пул — bootutil дважды дергает malloc даже без RTOS). Крипто-бэкенд
(mbedTLS/tinycrypt) уже вендорен в `ext/`.

---

## Фаза 0 — Карта Flash и место в системе сборки

**Цель**: зафиксировать бинарный контракт, прежде чем писать код.

- Определить смещения: `BOOTLOADER` (фикс. `0x60000000`, бюджет размера — по факту размера
  bootloader.bin + запас), `SLOT_A`, `SLOT_B` (равный размер, с запасом под рост tft_app; ориентир —
  `bsp_qspi_flash_size()` минус бюджет bootloader, поделить пополам). Direct-XIP не требует scratch-области
  — метаданные версии/статуса живут в самом image trailer каждого слота (стандарт bootutil).
  Зафиксировать в `docs/mimxrt1052/` рядом с существующими `BOOT_FLAGS.md`/`HAB_GUIDE.md` (новый файл
  `BOOTLOADER_FLASH_MAP.md` или раздел в `DEV_ARCH.md`) — единый источник истины для линкер-скриптов,
  `sysflash.h` и будущего tft_app.
- Зарегистрировать `firmware/bootloader` в корневом `CMakeLists.txt` (раскомментировать
  `add_subdirectory(firmware/bootloader)` — сейчас закомментировано вместе с `firmware/app`, добавить
  раздельно).

**Верификация**: ревью карты памяти (нет пересечений bootloader/Slot A/Slot B; согласуется с реальной
ёмкостью W25Q чипа на плате). Код ещё не пишем — это чисто согласованный документ.

---

## Фаза 1 — Скелет: собирается, грузится, живой (CDC + LED)

**Цель**: минимальный bootloader, который проходит тот же путь bring-up, что и `firmware_test`
(`firmware/test/src/main.c`), но без тестового раннера.

Новые файлы, по образцу `firmware/test/`:
- `firmware/bootloader/CMakeLists.txt` — target `bootloader`, линкер `MIMXRT1052xxxxx_flexspi_nor.ld`
  (без SDRAM-варианта — bootloader её не использует), `target_link_libraries`: `bsp_board`, `bsp_led`,
  `bsp_tick`, `bsp_button`, `bsp_usb_cdc`, `bsp_qspi_flash`, `bsp_boot_xip` (если применимо, как в
  firmware_test) + generated/startup/syscalls как в `firmware/test/CMakeLists.txt`.
- `firmware/bootloader/src/main.c` — bring-up: `board_hw_init()` → `bsp_led_init()` → `bsp_tick_init()`
  → `bsp_usb_cdc_init()` → мигание `LED_HEARTBEAT` до готовности CDC (копия паттерна из
  `firmware/test/src/main.c:30-65`).
- `firmware/bootloader/src/cli.c` + `protocol.c`/`.h` — урезанное подмножество протокола
  `firmware/test/src/protocol.h`: `ping`→`pong`, `get_version`→`version_response`, плюс новый
  `status`-эвент для состояний из Фазы 3/4 (без `test_begin`/`test_result`/`confirm_request` — те
  специфичны для firmware_test). Не шарить код с firmware_test напрямую (разные жизненные циклы
  сообщений) — копировать и урезать, как это уже сделано для `firmware_test_fatfs` vs будущего
  tft_app fatfs (см. комментарий в `firmware/test/fatfs/CMakeLists.txt:7-8`).
- HAB unsigned yaml для Debug (по образцу существующих конфигов firmware_test в `tools/host/hab/`).

**Верификация**:
1. ✅ `just build::build-bootloader-debug` — собирается (на хосте с `ARMGCC_DIR` вместо devcontainer —
   тоже работает, toolchain найден локально).
2. ✅ `just host::flash-swd-bootloader-debug` (готовый рецепт, уже существовал в `just/host.just`) —
   прошивается, `LED_HEARTBEAT` мигает до подключения CDC, после — `LED_APP` включается.
3. ✅ Ping/get_version по USB CDC ACM — `ping`→`pong`, `get_version`→`"0.1.0"`.
4. ✅ `🐛 Debug: bootloader` в `.vscode/launch.json` — подключается, останавливается на `main`.

**Отличия от исходного плана (по факту реализации):**
- Линкер-скрипт — не переиспользован общий `MIMXRT1052xxxxx_flexspi_nor.ld` (он нигде не использовался
  и не ограничивал `m_text` бюджетом bootloader), а сделана копия
  `cmake/linker/MIMXRT1052xxxxx_bootloader_flexspi_nor.ld` с `m_text` жёстко ограниченным 247 КБ
  (`ASSERT` на границу Slot A `0x60040000`, см. BOOTLOADER_FLASH_MAP.md) — превышение бюджета теперь
  ошибка линковки, а не тихий выход за пределы своей области. Фактически занято: 32.5 КБ (12.85%).
- `bsp_boot_xip` не переиспользован как есть (он тянет `XIP_BOOT_HEADER_DCD_ENABLE=1`, что противоречит
  решению "bootloader SDRAM не трогает"). Добавлен `bsp_boot_xip_no_dcd` в `bsp/CMakeLists.txt`, третий
  вариант рядом с существующими `bsp_boot_xip`/`bsp_boot_ram`.
- `status`-эвент из плана Фазы 1 отложен в Фазу 3/4 (там появится реальный смысл — состояния SD-установки
  и smoke-теста). Фаза 1 ограничена `ping`/`pong`/`get_version`/`version_response`/`error`, без
  `session_start` (не нужен — service-tui проверяет живость явным `ping`, как и HIL-фикстура
  `firmware_cdc` для firmware_test).
- Найден и исправлен баг scaffold'а: `🐛 Debug: bootloader` в `.vscode/launch.json` ссылался на
  несуществующую задачу `build-and-rtt:firmware-test-debug` (copy-paste). Исправлено на
  `build:bootloader-debug` (уже существовала в `tasks.json`).
- HAB Debug-конфиг (`tools/host/hab/hab_bootloader_debug.yaml`) уже существовал в исходном scaffold
  репозитория и уже был без DCD — менять не пришлось.
- `bsp_qspi_flash` и `bsp_button` из Фазы 1 в CMakeLists.txt пока не добавлены — они не используются
  до Фазы 2 (доступ к слотам) и Фазы 3 (downgrade-override), добавятся вместе с кодом, который их
  реально вызывает.

---

## Фаза 2 — bootutil: порт + выбор слота (host-тестируемая логика)

**Цель**: интегрировать bootutil в режиме Direct-XIP, с флеш-шимом над `bsp_qspi_flash`.

- `firmware/bootloader/mcuboot_port/` (новая директория): `flash_map_backend.c/.h` (шим
  `flash_area_open/read/write/erase` → `bsp_qspi_read`/`bsp_qspi_write_page`/`bsp_qspi_erase_sector`,
  по образцу `sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/flashapi/flash_api.c`), `sysflash.h`
  (2 flash-area ID под Slot A/Б, без scratch), `mcuboot_config.h` (`MCUBOOT_DIRECT_XIP`,
  `MCUBOOT_IMAGE_NUMBER=1`, выбор сигнатурной схемы — рекомендация: ECDSA P-256, компактные ключи и
  быстрая проверка на Cortex-M7 без аппаратного крипто-ускорителя), статический malloc/free-пул (bootutil
  дважды вызывает malloc в `loader.c` даже без RTOS).
- Подключить `sdk/middleware/mcuboot_opensource/boot/bootutil` как источник в CMake (аналогично тому, как
  `sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk` подключает bootutil через свой `.cmake`-файл —
  использовать `middleware_mcuboot_nxp_bootutil_port.cmake` как референс, не копировать вслепую).
- `firmware/bootloader/src/boot_select.c` — вызов `boot_go` (Direct-XIP путь), получение адреса entry
  point выбранного слота.

**Решения, принятые в обсуждении Фазы 2 (не пересматриваются):**
- Крипто-бэкенд — **TinyCrypt**, не mbedTLS: для ECDSA-P256+SHA-256 нужно 6 файлов (~2142 строк),
  всё уже вендорено (`sdk/middleware/mcuboot_opensource/ext/tinycrypt` + минимальный ASN.1-парсер из
  `ext/mbedtls-asn1`). Полноценный mbedTLS не вендорен вообще — потребовал бы ~8000+ новых строк.
- FIH-профиль — **MCUBOOT_FIH_PROFILE_LOW** (double-read защита + CFI-счётчики), без RNG-задержки
  (та требует профиль HIGH и реальную mbedTLS-энтропию — не наш случай).
- **MCUBOOT_DIRECT_XIP_REVERT — включён.** Если новый образ ни разу не подтверждён (`boot_set_confirmed()`,
  вызов — будущая ответственность tft_app), следующая загрузка стирает его и откатывается. Проверено
  host-тестом (`test_boot_go_reverts_unconfirmed_image`).
- Heap **не нужен** — `malloc`/`free` в `loader.c` компилируются только под `!MCUBOOT_DIRECT_XIP`.
  Линкер-скрипт бюджет 256 КБ (Фаза 1) не трогаем.
- Тестовый ключ — уже вендоренный публичный sample-ключ MCUboot
  (`sdk/middleware/mcuboot_opensource/root-ec-p256.pem`), публичная часть встроена как C-массив через
  `imgtool.py getpub --lang c` (`mcuboot_port/keys/bootloader_test_ecdsa_pub.c`). Не production-секрет —
  для серийного производства нужен отдельный ключ вне репозитория (аналог HAB SRK-церемонии).

**Итог host-части (выполнено):**
- `firmware/bootloader/mcuboot_port/` — `sysflash/sysflash.h`, `mcuboot_config/{mcuboot_config.h,
  mcuboot_logging.h}`, `flash_map_backend/flash_map_backend.h`, `flash_map.h`, `keys.c` +
  `keys/bootloader_test_ecdsa_pub.c`, `bootutil_sources.cmake` (общий список исходников bootutil +
  TinyCrypt + ASN.1, `include()`-ится и ARM-таргетом, и host-тестами — не дублируется).
- Финальный набор файлов bootutil для нашего режима (Direct-XIP + Revert, ECDSA-only, без
  measured-boot/encryption): `loader.c, bootutil_misc.c, bootutil_public.c, tlv.c, image_validate.c,
  image_ecdsa.c, fault_injection_hardening.c, swap_scratch.c`. Важная находка:
  **`swap_scratch.c` нужен несмотря на название** — `boot_read_image_header()` внутри него обёрнут в
  `#if !defined(MCUBOOT_SWAP_USING_MOVE)`, то есть это и есть дефолтная (не swap-move) реализация чтения
  заголовка слота, которую `loader.c` вызывает безусловно для любого режима, включая Direct-XIP.
  `swap_move.c` (с альтернативной версией той же функции под `MCUBOOT_SWAP_USING_MOVE`) не нужен.
- `tests/host/mcuboot_port/` — `fake_flash_map_backend.c/.h` (in-memory реализация контракта
  `flash_map.h` вместо `bsp_qspi_flash`), `host_link_shims.c` (см. ниже), `test_boot_select.c`
  (5 тестов), `fixtures/` (imgtool-подписанные `valid_v1.bin`/`valid_v2.bin`/`valid_v2_unconfirmed.bin`/
  `corrupt_v1.bin`, 32 КБ каждый, slot-size уменьшен относительно реальных 2 МБ — тестируем логику
  выбора слота, не абсолютные размеры).
- **Обход двух host-специфичных проблем линковки** (не существуют на реальном ARM-таргете,
  `arm-none-eabi-gcc` не использует leading-underscore mangling и `--gc-sections` вырезает мёртвый код):
  - `fih_panic_loop()` в `fault_injection_hardening.c` — ARM inline-asm self-reference по имени без
    подчёркивания. Зависит от ABI хоста, не просто от "это host-тест":
    **Mach-O (macOS)** — C-функция манглится в `_fih_panic_loop`, inline asm ищет
    `fih_panic_loop` без подчёркивания и не находит → нужен отдельный символ через
    `asm("fih_panic_loop")`-label.
    **ELF (Linux, напр. clang-17 в devcontainer)** — C-символы НЕ манглятся, `fih_panic_loop`
    резолвится сам на себя нативно, как на реальном ARM — наш шим здесь не нужен и ломает сборку
    (`multiple definition of 'fih_panic_loop'`, поймано на Release/Debug пресетах в devcontainer).
    Шим в `host_link_shims.c` обёрнут в `#if defined(__APPLE__)` — активен только там, где реально нужен.
  - `mbedtls_mpi_read_binary` — недостижимый RSA-путь ASN.1 (`mbedtls_asn1_get_mpi`), попадающий в
    объектный файл `asn1parse.c` целиком; на host (оба ABI) без `--gc-sections` требует явной
    (недостижимой по рантайму) заглушки — платформенно-независимая часть `host_link_shims.c`.
  - Проверено на обеих платформах: macOS (Homebrew clang, локально) и Linux/devcontainer (clang-17,
    Debug + Release пресеты) — `just build::test-host` и `test-host-release` зелёные 13/13 на обеих.
- **Обход бага clang 22.1.8 (Homebrew)**: `-fsanitize=address,undefined` ломает генерацию CFI-директив
  на больших функциях `loader.c` ("invalid CFI advance_loc expression" на этапе ассемблирования; без
  санитайзеров те же файлы собираются чисто). Отключены санитайзеры точечно для вендоренных файлов
  bootutil/TinyCrypt/ASN.1 через `set_source_files_properties` в `bootutil_sources.cmake` — для
  собственного кода (`fake_flash_map_backend.c`, тест) ASan/UBSan остаются включены.
- `just build::test-host` — **13/13 тестов зелёные**, включая 5/5 новых
  (`test_boot_go_slot_a_only_valid`, `test_boot_go_picks_higher_version`,
  `test_boot_go_ignores_corrupted_slot`, `test_boot_go_no_valid_image`,
  `test_boot_go_reverts_unconfirmed_image`).

**Осталось для Фазы 2 (ARM-сторона, ещё не сделано):**
- `firmware/bootloader/mcuboot_port/flash_map_backend.c` — реальный шим над `bsp_qspi_flash`
  (`bsp_qspi_read`/`bsp_qspi_write_page`/`bsp_qspi_erase_sector`), по образцу
  `sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/flashapi/flash_api.c`.
- `firmware/bootloader/src/boot_select.c` — `boot_go()` + прыжок в выбранный образ. Референс —
  `sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/boot.c::do_boot()`: `flash_device_base()` →
  `vt = flash_base + rsp->br_image_off + rsp->br_hdr->ih_hdr_size` → `__set_MSP(vt->msp)` →
  `((void(*)(void))vt->reset)()`. CMSIS-интринсики, ассемблер не нужен.
- Подключить `bsp_qspi_flash` в `firmware/bootloader/CMakeLists.txt`, собрать под ARM (проверить что
  `m_text` укладывается в 247 КБ бюджет Фазы 1).
- Аппаратная проверка: образ-заглушка (просто зажигает LED), подписанный тем же тестовым ключом, залит
  вручную через SWD в Slot A (`0x60040000`) — подтвердить что bootloader реально в него прыгает.

**Верификация — до всякого железа** (план, для истории):
- Host-юнит-тесты (`tests/host/`, Unity + fff, по образцу существующих `tests/host/protocol/`,
  `tests/host/cli/`) с фейковым flash-буфером в памяти вместо `bsp_qspi_flash`: валидный образ в
  Slot A только → выбран A; оба слота валидны, версия Б выше → выбран Б; повреждённый TLV/подпись в
  одном слоте → игнорируется, выбран валидный; оба слота пустые/невалидные → `boot_go` возвращает
  "нет образа" (это и есть триггер top-level состояния Фазы 3).
- Только после зелёных host-тестов — сборка `target-debug`/реальная плата с образом, подписанным
  `imgtool` (см. `sdk/middleware/mcuboot_opensource/scripts/imgtool.py`), прошитым вручную через SWD в
  Slot A — подтвердить, что bootloader реально прыгает в него (минимальный тестовый "app"-заглушка,
  которая просто включает LED, чтобы визуально подтвердить прыжок).

---

## Фаза 3 — SD-путь установки (единый для обоих производственных сценариев)

**Цель**: сканирование microSD, установка образа в слот, top-level состояние "нет валидного образа".

- `firmware/bootloader/fatfs/` — bare-metal FatFS-таргет по образцу `firmware/test/fatfs/`
  (свой `ffconf.h`, не шарить `firmware_test_fatfs` — bootloader и firmware_test взаимоисключающие
  прошивки на одной плате, зависимость от таргета с именем "firmware_test" в trust-anchor была бы
  неверной связью; переиспользуется общий `port/fatfs` уровнем ниже).
- `firmware/bootloader/src/sd_update.c` — на старте и опционально периодически: смонтировать SD,
  найти `TFT_APP.BIN` (imgtool-подписанный) в корне, распарсить заголовок (через bootutil), сравнить
  версию с активным слотом:
  - новее → стереть неактивный (или любой, если оба невалидны) слот, записать постранично через
    `bsp_qspi_write_page`, вычитать обратно и сверить хэш перед тем как считать установку завершённой;
  - старше/равно и кнопка `BSP_BUTTON_1` не удержана при старте → пропустить;
  - старше и кнопка удержана → тот же путь установки, что и "новее" (подпись всё равно проверяется).
- Top-level состояние "нет валидного слота": цикл ожидания SD с периодическим статусом на CDC
  (`status: waiting_for_sd`) и характерным LED-паттерном (Фаза 4 уточняет полный словарь паттернов) —
  выхода из цикла нет, пока установка не пройдёт успешно.

**Верификация (первая фаза, требующая реального железа)**:
1. Чистая плата (только bootloader, оба слота пустые) + SD с валидным подписанным образом →
   автоустановка, переход к загрузке (проверить по CDC-статусам и LED).
2. То же SD с образом версии ниже уже установленной, кнопка не нажата → отклонён, лог на CDC.
3. То же, кнопка `BSP_BUTTON_1` удержана при старте → установлен.
4. SD с образом без подписи/битым TLV → отклонён, плата остаётся на последнем валидном слоте (если был)
   или продолжает ждать (если не было), характерный error-паттерн LED.
5. Сценарий 2 (бандл залит сразу в Slot A через SWD/SDP на этапе тестирования): SD не участвует,
   bootloader должен просто загрузить Slot A без обращения к SD-логике вообще.

---

## Фаза 4 — SDRAM/W25Q smoke-test + словарь LED-паттернов

**Цель**: диагностический, неблокирующий проход по SDRAM/W25Q + финальный набор статусов.

- Новая функция подъёма SEMC (например `bsp_sdram_configure()` рядом с существующим `bsp_sdram_init()`
  в `bsp/sdram/`) — переносит регистровую последовательность, которая сейчас зашита в DCD, в вызываемый
  C-код. Bootloader вызывает её только на время smoke-теста; tft_app (в своём будущем плане) будет
  вызывать её по-настоящему в раннем startup.
- Smoke-test в `main.c`: `bsp_qspi_init()` (уже обязателен для доступа к слотам — статус даром),
  `bsp_sdram_configure()` + `bsp_sdram_init()` (опционально, по времени — не блокирует переход к
  загрузке приложения; результат только репортится).
- Финальный словарь состояний на `LED_APP` (поверх уже реализованных в Фазах 1/3):
  ожидание SD / установка / ошибка образа / smoke-test fail / переход в app (гаснет).
- `status`-события на CDC для каждого состояния — `tools/service_tui` сможет опрашивать их так же, как
  сейчас `FirmwareClient.ping()`/`get_version()`.

**Верификация**: прогон на заведомо исправной плате — все статусы (`waiting_for_sd`, `installing`,
`smoke_pass`/`smoke_fail`, `booting`) наблюдаются в правильном порядке через CDC и глазами по LED.
Полноценной аппаратной инъекции неисправностей не делаем (в проекте и для остальных HIL-тестов это не
принято) — ограничиваемся проверкой на реальной плате в штатном состоянии.

---

## Фаза 5 — HAB, релизные пресеты, интеграция в service-tui

**Цель**: замкнуть производственный цикл для сценария "bootloader-only, потом SD".

- HAB Release yaml для bootloader (подписанный, `flags=0x08`) — по образцу существующих
  `just/build.just:154-176` (`hab-bootloader-debug/release` уже определены, нужен только рабочий
  `bootloader_hab.bin`).
- `tools/service_tui`: обобщить/расширить `FirmwareClient` (или добавить параллельный тонкий клиент,
  переиспользующий его внутренности) так, чтобы после прошивки bootloader через SDP service-tui сразу
  проверял `ping`→`pong` и `get_version` — это и есть запрошенная "обратная связь, что загрузчик жив"
  для сценария 1 (плата отложена в кучу без tft_app).
- Проверить, что уже существующие `just host::flash-production` / `just host::production`
  (`just/host.just:532-535`) корректно находят `bootloader_hab.bin` — сборка `app_hab.bin` пока
  недоступна (tft_app не реализован), так что сквозной прогон "production" целиком верифицируется только
  после отдельного плана на tft_app; в рамках этой фазы проверяем только bootloader-плечо.

**Верификация**: чистая плата → `just host::flash-production`-путь для bootloader (или его bootloader-only
подмножество) → service-tui показывает "bootloader alive" на основе реального ping/version с платы.

---

## Ключевые файлы для переиспользования (не изобретать заново)

| Что нужно | Где смотреть образец |
|---|---|
| Bring-up последовательность | `firmware/test/src/main.c` |
| JSON-lines протокол (ping/pong/version) | `firmware/test/src/protocol.h`, `.c` |
| CLI-диспетчер команд | `firmware/test/src/cli.c` |
| CMake-паттерн firmware-таргета + HAB post-build | `firmware/test/CMakeLists.txt` |
| Bare-metal FatFS-таргет | `firmware/test/fatfs/CMakeLists.txt` |
| Flash R/W/erase/JEDEC | `bsp/qspi_flash/include/bsp/qspi_flash.h` |
| SDRAM verify (паттерн read/write) | `bsp/sdram/src/sdram.c` |
| LED heartbeat/app конвенция | `bsp/led/include/bsp/led.h` |
| Кнопки с дебаунсом | `bsp/button/include/bsp/button.h` |
| bootutil NXP-порт (референс для шима) | `sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/` |
| Линкер-скрипт mcuboot для RT1052 | `sdk/boards/evkbimxrt1050/ota_examples/ota_mcuboot_basic/armgcc/MIMXRT1052xxxxx_flexspi_nor_mcuboot.ld` |
| imgtool (подпись образов) | `sdk/middleware/mcuboot_opensource/scripts/imgtool.py` |
| CDC-клиент на стороне service-tui | `tools/service_tui/app/firmware_client.py` |
| Host-юнит-тесты (Unity+fff паттерн) | `tests/host/protocol/`, `tests/host/cli/` |

## Общий принцип верификации по фазам

Логика без железа (выбор слота, парсинг TLV, версия-компар) — **host-тесты** (`just build::test-host`),
используя уже принятую в проекте связку Unity+fff с фейковым flash-буфером — так же, как уже тестируются
`cli`/`protocol`/`prio_queue`. Всё, что требует реального железа (SD, запись во flash, SEMC, LED,
USB CDC) — только после того как логика зелёная на хосте, ручная проверка на плате по чек-листу каждой
фазы. Не начинать следующую фазу, пока не пройдена верификация текущей.
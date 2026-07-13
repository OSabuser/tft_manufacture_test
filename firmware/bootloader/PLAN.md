# Загрузчик (firmware/bootloader) — план разработки по фазам

## Статус

| Фаза | Статус | Примечание |
|---|---|---|
| 0 — Карта Flash | ✅ завершена | [docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md](../../docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md) |
| 1 — Скелет (CDC + LED) | ✅ завершена | сборка/HAB/SWD-прошивка/ping-pong/debug — все пункты верификации пройдены на реальной плате, детали ниже |
| 2 — bootutil (Direct-XIP) | ✅ завершена | host-тесты 5/5, аппаратная верификация — все 5 сценариев пройдены на реальной плате (детали и 3 найденных/исправленных бага — [DEBUG_LOG_PHASE2.md](DEBUG_LOG_PHASE2.md)) |
| 3 — SD-путь установки | ✅ верифицирована (2026-07-10); раунд 4 + watchdog ждут ре-проверки | все 5 сценариев пройдены на плате; баг card-detect закрыт чтением USDHC PRES_STATE.CINST (раунд 3). Раунд 4 (консолидация детекта на единый PRSSTAT + ранний сэмпл кнопки) и **аппаратный watchdog** (`bsp/wdog`, см. ниже) реализованы, сборка зелёная, **аппаратно ещё не проверены**. См. [DEBUG_LOG_PHASE3_SD.md](DEBUG_LOG_PHASE3_SD.md), чек-лист — [test_stub/HARDWARE_VERIFICATION_PHASE3.md](test_stub/HARDWARE_VERIFICATION_PHASE3.md) |
| 4 — SDRAM/W25Q smoke-test + LED-паттерны | не начата | рекомендуется ПОСЛЕ Фазы 6 (см. ниже) |
| 5 — HAB Release + service-tui | не начата | |
| 6 — Устойчивость и восстановление (recovery) | спроектирована, код не начат | **рекомендованный следующий шаг** (раньше 4/5): закрывает зависание уже установленного образа и даёт ручной аварийный вход. Дизайн зафиксирован в обсуждении — см. раздел «Фаза 6» ниже |

## Контекст

`service-tui` и `firmware_test` уже выпущены и работают на производстве. Следующий шаг —
`firmware/bootloader/` (сейчас пустая директория, но сборочная инфраструктура под неё уже
существует: пресеты `bootloader-debug/release` в CMakePresets.json, рецепты `just build::*`,
`.vscode/launch.json`, HAB-конвейер) и `firmware/tft_app/`. Начинаем с загрузчика — он определяет
контракт (flash-layout, формат образа, версия), под который потом пишется tft_app.

Архитектурные решения, принятые в обсуждении (не пересматриваются в рамках этого плана):

- **Boot-стратегия**: и bootloader, и tft_app исполняются XIP из W25Q. Bootloader без ITCM-копирования,
  без DCD — он не трогает SDRAM. tft_app сама поднимает SEMC в своём раннем startup (SDRAM — под её XIP,
  см. отдельный будущий план на tft_app). Пересмотрена и переподтверждена в обсуждении Фазы 3 (риск
  конфликта flash-AHB с runtime-записью настроек/проигрыванием аудио в tft_app vs `MCUBOOT_RAM_LOAD` —
  см. [BOOTLOADER_FLASH_MAP.md §5](../../docs/mimxrt1052/BOOTLOADER_FLASH_MAP.md#5-ограничения-на-runtime-доступ-к-flash-из-tft_app-direct-xip--решено)).
- **Схема обновления**: MCUboot **Direct-XIP**, два слота (A/Б) с полностью валидными образами каждый,
  без swap/scratch. Единственный полевой канал обновления — microSD. USB как канал заливки *образа*
  сознательно не делаем (SDP/blhost на производстве — это отдельный, не зависящий от кода bootloader,
  канал через NXP BootROM). **Direct-XIP → образ линкуется под адрес своего слота**: релиз tft_app =
  два подписанных бинаря (линковка под A и под Б) на версию. Полный разбор производственной заливки и
  полевого обновления, включая текущий gap в `sd_update` (один `TFT_APP.BIN`) —
  [docs/mimxrt1052/UPDATE_FLOW.md](../../docs/mimxrt1052/UPDATE_FLOW.md).
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

**ARM-сторона (выполнено):**
- `firmware/bootloader/mcuboot_port/flash_map_backend.c` — реальный шим над `bsp_qspi_flash`.
  `bsp_qspi_read/write_page/erase_sector()` принимают flash-relative адрес (0-based, `IPCR0` FlexSPI
  IP-команд), НЕ XIP-адрес — `flash_area.fa_off` тоже flash-relative (Slot A `0x00040000`, Slot Б
  `0x00240000`). `flash_device_base()` — единственное место с XIP-адресом `0x60000000`, нужен только
  `boot_select.c` для вычисления адреса прыжка. Постраничная запись (`write_page_chunked`) — порт
  логики `flash_area_write_internal()` из NXP-референса: 0xFF поверх уже запрограммированных байт не
  меняет их (NOR program только сбрасывает биты 1→0), поэтому безопасно перезатирать буфером с
  ERASED_VAL в нетронутой части страницы.
- `firmware/bootloader/src/boot_select.{c,h}` — `boot_go()` + `jump_to_image()`. Портировано с
  `sdk/middleware/mcuboot_opensource/boot/nxp_mcux_sdk/boot.c::do_boot()`: `flash_device_base()` →
  `vt = flash_base + rsp->br_image_off + rsp->br_hdr->ih_hdr_size` → `__set_MSP(vt->msp)` →
  `((void(*)(void))vt->reset)()`. CMSIS-интринсики (`fsl_common.h`), ассемблер не понадобился.
- `firmware/bootloader/CMakeLists.txt` — `bsp_qspi_flash` в зависимостях,
  `include(mcuboot_port/bootutil_sources.cmake)` (тот же список bootutil+TinyCrypt+ASN.1, что и у
  host-тестов — не дублируется). `-w`/`-fno-sanitize` для вендоренного кода перенесены в сам
  `bootutil_sources.cmake` (`set_source_files_properties`), не блэнкетом на весь таргет — наш код
  (`main.c`, `boot_select.c`, `flash_map_backend.c`) остаётся под обычными warnings.
- `main.c`: после `board_hw_init()`/`bsp_led_init()`/`bsp_tick_init()`/`bsp_qspi_init()` — сразу
  `boot_select_and_jump()`, **до** поднятия USB CDC. Так плата грузится в tft_app и без подключённого
  кабеля (нормальный полевой сценарий) — ждать хоста для проверки образа было бы неправильно. Провал
  → **не падать**, продолжить в уже существующий ping/pong-цикл Фазы 1 — прообраз будущего состояния
  "жду SD" из Фазы 3, без самого SD-сканирования. Сценарий "оба слота пусты" на железе проверяется тем
  же CDC-ping, что и в Фазе 1.
- Собрано под ARM: `m_text` — 50712 Б из 247 КБ бюджета (20.05%, был 12.85% в Фазе 1 — рост за счёт
  bootutil+TinyCrypt+ASN.1, запас всё ещё большой). `just build::build-bootloader-debug` и
  `hab-bootloader-debug` — оба зелёные.

### Аппаратная верификация Фазы 2 — все 5 сценариев пройдены на железе

**Зачем отдельная заглушка.** `tft_app` не существует — нечего класть в слоты для проверки прыжка.
Нужен минимальный, независимо собираемый "образ", который bootloader может реально выбрать и в
который может реально прыгнуть — не файл с мусором, а настоящий imgtool-подписанный образ с корректным
vector table по адресу слота.

**Реализовано в `firmware/bootloader/test_stub/`** (не `tests/target/` — тот масштабируется под
RAM-загрузку через pyOCD для HIL, наш стаб — XIP из Flash, ближе по духу к самому bootloader; удалить
директорию целиком, когда появится реальный `firmware/tft_app`):
- `main.c` — `board_hw_init()` → `bsp_led_init()` → `bsp_tick_init()` → бесконечный цикл
  `bsp_led_toggle(LED_APP)` с периодом `STUB_BLINK_MS` (компилируется в двух вариантах). Без USB/CDC.
- `cmake/linker/MIMXRT1052xxxxx_mcuboot_slot.ld` — параметризован через `-Wl,--defsym=__slot_base__=`,
  один файл на оба таргета вместо двух копий. `ORIGIN = SLOT_BASE + 0x200` (после imgtool header),
  `m_text` 32 КБ (с большим запасом для мигалки в 2-МБ слоте). Пришлось добавить `m_data2`/
  `__NCACHE_REGION_START/SIZE` — `board_mpu_init()` (общий для всех прошивок, включён транзитивно
  через `bsp_board`) их безусловно требует, даже когда некэшируемый регион не используется.
- Два CMake-таргета из одного `main.c` (`add_mcuboot_stub()` в `test_stub/CMakeLists.txt`):
  `test_slot_stub_a` (`__slot_base__=0x60040000`, `STUB_BLINK_MS=500` — мигает ~1 раз/сек, "версия 1"),
  `test_slot_stub_b` (`__slot_base__=0x60240000`, `STUB_BLINK_MS=250` — ~2 раза/сек, "версия 2").
  Разный адрес — намеренно: MCUboot Direct-XIP код обычно не позиционно-независим (открытый вопрос из
  BOOTLOADER_FLASH_MAP.md §4), это первая реальная проверка two-slot-two-linkage подхода.
- Оба собраны и подписаны тем же тестовым ключом (`sdk/middleware/mcuboot_opensource/root-ec-p256.pem`),
  реальные параметры слота (`-H 0x200 -S 0x200000 --align 1 --pad-header --pad`) — **одной командой**:

  ```bash
  just build::build-mcuboot-stub
  ```

  Рецепт (`just/build.just`, группа `mcuboot-stub`) сам собирает `test_slot_stub_a`/`test_slot_stub_b`
  (пресет `mcuboot-stub-debug`, targets в `CMakePresets.json`, т.к. в `bootloader-debug` их нет — иначе
  собирались бы всегда вместе с bootloader) и подписывает imgtool'ом (эфемерный venv через
  `uv run --with cryptography --with intelhex --with click --with cbor2 --with pyyaml` — эти зависимости
  не в `tools/host` (spsdk), туда их специально не добавляли, чтобы не раздувать основной venv ради
  временной тестовой оснастки). **Важно**: сначала это было проделано вручную в чате и `signed/` не
  появлялась при обычной пересборке — это и вскрылось при попытке воспроизвести. Плюс была реальная
  ошибка в первой версии рецепта: потерян `--pad` у `unconfirmed`-варианта (файл получался 16 КБ вместо
  2 МБ — без trailer'а в конце слота revert-сценарий не работал бы). Исправлено, проверено размерами
  файлов (все три — по 2 МБ).
  - `build/Debug/signed/stub_a_v1_confirmed.bin` — `-v 1.0.0 --confirm` (сценарии 1, 3, 4)
  - `build/Debug/signed/stub_b_v2_confirmed.bin` — `-v 2.0.0 --confirm` (сценарий 2)
  - `build/Debug/signed/stub_a_v1_unconfirmed.bin` — `-v 1.0.0`, без `--confirm` (сценарий 5, revert)

**Прошивка в слот напрямую по адресу** — не через `flash_swd.py` (тот собирает FCB+IVT+HAB под
`0x60000000`, слотам это не нужно — они не самостоятельный boot-образ для BootROM, а данные, которые
читает `boot_go()`). Пишем сырой подписанный `.bin` напрямую по адресу слота через pyOCD (тот же
`tools/hil` venv, что уже использует `flash_swd.py` — см. `run_pyocd_flash()`):

**Важно**: `uv run --directory tools/hil` меняет рабочую директорию у самого `pyocd`, а не только у
`uv` — относительный путь к `.bin` резолвится от `tools/hil/`, не от корня репозитория. Путь к образу
должен быть абсолютным (`"$(pwd)/build/..."`, запускать из корня репо).

```bash
# Slot A — валидный, confirmed (сценарии 1, 3, 4)
uv run --directory tools/hil pyocd flash --target mimxrt1050_quadspi --frequency 4000000 \
  --base-address 0x60040000 --erase sector "$(pwd)/build/Debug/signed/stub_a_v1_confirmed.bin"

# Slot Б — валидный, confirmed, новее (сценарий 2)
uv run --directory tools/hil pyocd flash --target mimxrt1050_quadspi --frequency 4000000 \
  --base-address 0x60240000 --erase sector "$(pwd)/build/Debug/signed/stub_b_v2_confirmed.bin"

# Slot A — неподтверждённый, для проверки revert (сценарий 5, вместо confirmed-варианта выше)
uv run --directory tools/hil pyocd flash --target mimxrt1050_quadspi --frequency 4000000 \
  --base-address 0x60040000 --erase sector "$(pwd)/build/Debug/signed/stub_a_v1_unconfirmed.bin"

# Стереть слот (для сценария 4 — "оба слота пусты")
# Адрес диапазона — позиционный аргумент, не через -a; start+length, не @.
uv run --directory tools/hil pyocd erase --target mimxrt1050_quadspi --frequency 4000000 \
  --sector 0x60040000+0x200000
uv run --directory tools/hil pyocd erase --target mimxrt1050_quadspi --frequency 4000000 \
  --sector 0x60240000+0x200000
```

**Чек-лист (зеркалит 5 host-тестов, но на реальном железе и с реальным `bsp_qspi_flash`) — все 5
сценариев пройдены на плате (2026-07-09):**

| № | Сценарий | Подготовка | Ожидаемый результат |
|---|---|---|---|
| 1 | Валиден только Slot A | Erase Slot Б, `stub_a_v1_confirmed.bin` → Slot A | LED_APP мигает ~1/сек (частота stub_a) |
| 2 | Оба валидны, побеждает версия Б | + `stub_b_v2_confirmed.bin` → Slot Б | LED_APP мигает ~2/сек (частота stub_b) |
| 3 | Slot Б повреждён | В Slot Б — испорченный файл (например, скопировать `stub_b_v2_confirmed.bin`, поменять байт в payload, прошить) | LED_APP возвращается к ~1/сек (Slot A) |
| 4 | Оба слота пусты | Erase Slot A и Slot Б целиком | LED_APP не мигает по образцу заглушки; `ping` по CDC отвечает `pong` — bootloader не прыгнул, остался в своём цикле |
| 5 | Revert неподтверждённого образа | `stub_a_v1_unconfirmed.bin` → Slot A; power cycle (1) → LED мигает (выбран впервые, `copy_done` выставляется); power cycle (2) БЕЗ вмешательства → Slot A должен быть стёрт bootutil'ом | После второго ресета — как сценарий 4 (LED не мигает, CDC ping жив) |

Между сценариями — обязательный power cycle (SWD-запись не ресетит автоматически, как и в Фазе 1).

**Инструмент подтверждения "прыжок реально произошёл", а не просто "LED мигает случайно":** частота
мигания однозначно указывает на конкретный слот (500 мс vs 250 мс визуально различимы), так что
чек-лист верифицируем глазами без дополнительной телеметрии. Если нужна более строгая проверка —
можно снять частоту осциллографом/логическим анализатором через MCU-Link, но для Фазы 2 визуального
контроля достаточно.

### Найденный баг: сценарий 1 не проходил — LED мигал один раз и замирал

`jump_to_image()` (`boot_select.c`) вызывал `__disable_irq()` перед прыжком — "для чистоты", не было
в референсном `do_boot()` NXP. `bsp_delay()` (`bsp/tick/src/tick.c`) — busy-wait на `g_s_tick_ms`,
инкрементируемом только внутри `SysTick_Handler` (ISR). Обычный `Reset_Handler` целевого образа не
трогает PRIMASK — рассчитывает, что прерывания уже разрешены, как после настоящего аппаратного
ресета. Замаскировав IRQ прыжком и не восстановив их нигде, мы гарантированно вешали `bsp_delay()` в
любом образе, куда прыгает bootloader: `LED_APP` успевал toggle'нуться один раз (до первого
`bsp_delay()` внутри цикла стаба) и застывал — неотличимо на глаз от "не мигает вообще".

**Исправлено**: убрали `__disable_irq()` из `jump_to_image()` — как и в референсе, трогаем только
`__set_CONTROL(0)`/`__set_MSP`/`__ISB`. Пересобрано (`m_text` уменьшился на 272 Б), HAB
пересгенерирован. Стаб-образы (`build/Debug/signed/*.bin`) пересборки не требуют — баг был чисто на
стороне bootloader, не заглушки.

### Второй найденный баг: сценарий 1 всё ещё не проходил — реальный завис в `bsp_qspi_flash`

После фикса `jump_to_image()` завис уже сам bootloader, **до** прыжка — в отладчике (стек
`boot_select_and_jump → boot_go → ... → bootutil_img_validate → bootutil_img_hash → flash_area_read →
bsp_qspi_read → qspi_ip_read → qspi_read_fifo → qspi_read_tail`) видно бесконечный busy-wait на
`IPRXFSTS.FILL`. Значения на момент зависания: `remain=7` (внутри `qspi_read_tail`, т.е. запрошено
`WORDS_NEEDED=2` слова), `FILL` стабильно `1`, `QSPI_BASE->INTR=0x61` — расшифровка битов
(`PERI_FLEXSPI.h`): bit0 `IPCMDDONE` **уже установлен**. Контроллер считает IP-команду завершённой,
реально доставив только одно слово (4 байта) из требуемых двух.

`bootutil_img_hash()` (`image_validate.c:125-129`) читает образ кусками до `BOOT_TMPBUF_SZ=256` байт;
последняя итерация цикла — остаток `size - off`, в данном случае 7 байт (последние байты хэшируемой
области `header+img_size+protected_tlv`). Эта короткая, **не кратная 4** длина ни разу не встречалась
раньше: JEDEC ID и чтения статус-регистров всегда используют `SR_READ_LEN=4` (уже word-aligned), а
`firmware_test`/`test_qspi.c` тоже, судя по всему, ни разу не запрашивал не кратный 4 размер. LUT
`LSEQ_IP_READ` (`qspi_flash.c`) использует `READ_SDR` с operand `0x04` — похоже, что при `IDATSZ`, не
кратном 4, контроллер отдаёт ровно один "бит" этой инструкции и не дотягивает до второго (частичного)
слова.

**Исправлено** в `bsp/qspi_flash/src/qspi_flash.c::qspi_ip_read()` — `IDATSZ`, который уходит в
железо (`qspi_ip_setup`), теперь округляется вверх до кратного `QSPI_RFDR_WORD_BYTES` (4); из FIFO
`qspi_read_fifo()`/`qspi_read_tail()` по-прежнему извлекают ровно исходное (не округлённое) число
байт — лишний padding-байт молча дренируется вместе со словом и отбрасывается существующей логикой
извлечения, менять её не понадобилось. Затрагивает **все** IP-чтения произвольной длины через
`bsp_qspi_read()`, не только bootutil — потенциально тот же баг мог бы всплыть и в `firmware_test`,
если бы там когда-нибудь понадобилось прочитать не кратное 4 число байт.

**Важно**: после этого фикса на сценарии 1 всплыл ещё один, третий баг (ниже) — оба фикса стоят в
дереве вместе, изолированно друг от друга на железе не перепроверялись.

**Урок**: любая ручная "гигиена" вокруг прыжка (маскирование прерываний, сброс периферии и т.п.),
не присутствующая в проверенном референсе, — повод для отдельного вопроса "а точно ли это
симметрично восстанавливается на другой стороне", а не молчаливого добавления "на всякий случай".

### Третий найденный баг: зависание переехало на чтение подписи — `qspi_read_tail` сравнивал FILL в юнитах напрямую со счётчиком слов

После фикса второго бага зависание не пропало, а переехало дальше по стеку — тот же паттерн
(`qspi_read_tail`, `FILL` намертво на `1`), но уже на чтении TLV с ECDSA-P256 подписью в
`bootutil_img_validate` (не в `bootutil_img_hash`). Ключевой момент: для этого чтения `IDATSZ`,
уходящий в железо, уже был кратен 4 (72 байта) — гипотеза про word-alignment из второго бага оказалась
неполной.

Настоящая причина: `IPRXFSTS.FILL` считает не слова, а watermark-юниты по 8 байт (2 слова) — как и
документирует `FLEXSPI_GetFifoCounts()` в SDK-драйвере (`fsl_flexspi.h`, домножает то же поле на 8
при переводе в байты). `qspi_read_tail()` сравнивал `FILL` напрямую со счётчиком нужных слов, без
перевода единиц — зависал именно тогда, когда хвост требовал ровно 2 слова (невидимо для всех чтений,
которым достаточно 1 слова, включая все SR/JEDEC-чтения — отсюда и не проявлялось раньше).

**Исправлено** в той же функции `qspi_read_tail()` — сравнение переведено в слова
(`FILL_UNITS * QSPI_WM_UNIT_WORDS >= WORDS_NEEDED`), тем же паттерном, что уже использовался в
`qspi_read_fifo()`. Полный разбор, включая то, как гипотеза была подтверждена без доступа к живому
регистру через отладчик (упёрлись в ограничение карты памяти pyOCD-таргета `mimxrt1050_quadspi`) —
[DEBUG_LOG_PHASE2.md](DEBUG_LOG_PHASE2.md).

**Подтверждено на железе**: со всеми тремя фиксами сценарий 1, а следом и оставшиеся четыре сценария
чек-листа прошли (2026-07-09). Фаза 2 аппаратно верифицирована полностью.

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

### Ключевая находка (определила архитектуру, до всякого кода)

**`boot_go()` нельзя вызывать дважды за одну сессию питания.** В `loader.c::boot_select_or_erase()`
(Direct-XIP-Revert) при выборе ещё не подтверждённого образа функция сразу пишет `copy_done=SET` **в
трейлер во flash** и продолжает грузить его. Если в той же сессии вызвать `boot_go()` повторно (напр.
чтобы сначала "подсмотреть" активную версию, а потом прыгнуть) — второй вызов увидит `copy_done=SET`,
`image_ok` ещё не выставлен (приложение не успело подтвердиться) — и **сотрёт только что установленный
образ**, посчитав это неудавшимся revert'ом. Это же поведение уже неявно доказано существующим
host-тестом Фазы 2 (`test_boot_go_reverts_unconfirmed_image` в `tests/host/mcuboot_port/`): второй
`boot_go()` на неподтверждённом образе стирает его.

Следствие: версию уже активного слота для сравнения с SD-кандидатом нужно узнавать **не через
`boot_go()`**, а отдельным read-only способом (`slot_version.c`, ниже) — сам `boot_go()` (через
`boot_select_and_jump()`) вызывается в `main.c` ровно один раз за попытку, в самом конце, уже после
того как вся SD-логика отработала и решение об установке принято.

### Решения, принятые в обсуждении Фазы 3 (не пересматриваются)

- **Пик версии активного слота — полная криптографическая проверка**, не пик только заголовка.
  `slot_version_get()` (`firmware/bootloader/src/slot_version.c`) читает заголовок слота и вызывает
  `bootutil_img_validate()` напрямую (тот же вызов, что `loader.c::boot_image_check()` использует
  внутри `boot_go()`, вне state-машины `context_boot_go()`) — hash+ECDSA проверяются по-настоящему,
  не только magic. `bootutil_img_validate()` не пишет в flash (проверено по `image_validate.c`) и
  безопасна вызывать сколько угодно раз, включая до первого `boot_go()` и на ещё не подтверждённых
  образах — в отличие от `boot_go()`. `FIH_CALL`-обёртка (CFI-счётчик, `FIH_ENABLE_CFI` под профилем
  LOW) самобалансируется на каждый вызов независимо (save/increment перед вызовом, decrement/verify
  после — см. `fault_injection_hardening.h`), поэтому несколько вызовов подряд (Slot A, Slot Б,
  повторно после установки) и последующий отдельный `boot_go()` не влияют друг на друга.
- **Гейт принятия SD-кандидата — двухступенчатый**, чтобы не строить отдельный flash_area-шim над
  SD-файлом: (1) лёгкий пик заголовка (magic + версия) прямо с SD, до касания flash — этого достаточно
  для решения install/skip; (2) после записи в целевой слот — тот же `slot_version_get()` на этом
  слоте как финальный крипто-гейт. Если подпись кандидата битая — `slot_version_get()` вернёт false
  (событие `SD_INSTALL_REJECTED`), а последующий единственный `boot_go()` просто не выберет этот слот
  и останется на прежнем валидном — сама проверка подписи не дублируется нигде отдельно.
- **Стирание целого слота — fast-path на 64 КБ блоках** внутри самого `flash_area_erase()`
  (`mcuboot_port/flash_map_backend.c`): если стирается вся область целиком (`off=0`, `len=fa_size`,
  кратно 64 КБ) — `bsp_qspi_erase_block_64k()` (~4.8 с на 2 МБ) вместо посекторного пути (~23 с).
  Частичное/невыровненное стирание — прежний посекторный путь. Выигрыш получает и `sd_update`, и
  штатный revert-erase внутри `boot_go()` (`boot_select_or_erase()`) — оба уже зовут
  `flash_area_erase(fap, 0, flash_area_get_size(fap))` без каких-либо изменений в bootutil-коде.
- **Верификация записи — немедленный `memcmp` на каждый чанк**, не хэш всего образа. Сразу после
  `flash_area_write()` очередного чанка (4 КБ) — `flash_area_read()` того же диапазона и побайтовое
  сравнение. Дёшево (буфер уже есть), ловит битую страницу сразу, без второго прохода по SD/flash и
  без дублирования того, что `bootutil_img_validate()` и так проверит через TLV-хэш на шаге 2 гейта
  выше.
- **Цикл ожидания SD — периодический пере-скан**, не одноразовая проверка при холодном старте.
  Существенно для производственного сценария "bootloader-only → потом массовая SD-установка" — SD-путь
  не должен требовать, чтобы карта уже стояла в момент включения питания. Дросселирован
  `SD_RETRY_PERIOD_MS = 1500` мс через `bsp_tick_get_ms()`, не завязан на период мигания LED.
- **Целевой слот установки — всегда НЕ активный.** Активный слот (валидный, с более высокой версией;
  если валиден только один — он активный; если ни одного — активного слота нет) этой логикой никогда
  не стирается и не перезаписывается, независимо от исхода сравнения версий и от кнопки. Если
  активного слота нет вообще — по умолчанию Slot A. Инвариант живёт в чистой функции
  `update_policy_decide()` (`firmware/bootloader/src/update_policy.c`), полностью host-тестируемой (без
  флеша/SD).
- **USB CDC поднимается до SD-логики**, не дожидаясь подключения хоста — `bsp_usb_cdc_write()` не
  блокируется без хоста (см. `bsp/usb_cdc/src/usb_cdc.c:585-617`, безопасно проверено по коду), поэтому
  статусы (`status: installing`, `waiting_for_sd`) видны технологу, если он уже подключён, в т.ч. на
  самой первой попытке (чек-лист, сценарий 1). Небольшой сопутствующий эффект: `bsp_usb_cdc_init()`
  теперь вызывается на каждой загрузке (раньше — только если `boot_select_and_jump()` уже провалился);
  сама инициализация нерегистрозатратна и не ждёт хоста, так что "быстрая загрузка без кабеля" не
  нарушена.
- **`bootloader_fatfs`** — bare-metal FatFS-таргет по образцу `firmware/test/fatfs/` (свой `ffconf.h`,
  не шарить `firmware_test_fatfs` — bootloader и firmware_test взаимоисключающие прошивки на одной
  плате). Единственное отличие от `firmware_test_fatfs`: **`FF_FS_READONLY=1`** — bootloader только
  читает `TFT_APP.BIN`, никогда не пишет на SD; убирает весь write-путь FatFS из сборки.

### Итог (выполнено, аппаратно ещё не проверено)

- `firmware/bootloader/src/update_policy.{c,h}` — чистая логика install/skip + выбор целевого слота
  (см. решения выше). `image_version_compare()` — свой аналог `boot_version_cmp()` (`static` в
  `loader.c`, не экспортируется): major.minor.revision, без build_num, то же соглашение по умолчанию.
  12 host-тестов (`tests/host/update_policy/`) — все сценарии чек-листа ниже плюс сравнение версий.
- `firmware/bootloader/src/slot_version.{c,h}` — read-only пик версии слота (см. находку выше).
  4 host-теста (`tests/host/slot_version/`), переиспользуют `fake_flash_map_backend.c` и фикстуры
  `tests/host/mcuboot_port/fixtures/` из Фазы 2 (`valid_v1/v2`, `corrupt_v1`,
  `valid_v2_unconfirmed` — последний доказывает, что пик не задет revert-состоянием, в отличие от
  `boot_go()`).
- `firmware/bootloader/mcuboot_port/flash_map_backend.c` — fast-path 64К в `flash_area_erase()`.
- `firmware/bootloader/fatfs/` — `bootloader_fatfs` (CMakeLists.txt, `include/ffconf.h`, `src/diskio.c`),
  подключена через `add_subdirectory(fatfs)` в `firmware/bootloader/CMakeLists.txt`.
- `firmware/bootloader/src/sd_update.{c,h}` — оркестрация: `bsp_sd_is_inserted()` → `bsp_sd_init()` →
  `f_mount("2:/")` → `f_open("2:/TFT_APP.BIN")` → лёгкий пик заголовка → `slot_version_get()` на
  обоих слотах → `update_policy_decide()` → при INSTALL: `protocol_send_status("installing")` →
  `flash_area_erase` (fast-path) + потоковое копирование чанками 4 КБ (`flash_area_write` +
  немедленный `memcmp`-verify) → финальный `slot_version_get()` на целевом слоте как крипто-гейт.
  Коды ошибок на CDC: `SD_CANDIDATE_INVALID`, `SD_INSTALL_WRITE_FAILED`, `SD_INSTALL_REJECTED`. Не
  host-тестируется напрямую (тонкий ARM-only оркестратор над уже протестированными
  `update_policy`/`slot_version`+ реальной FatFS/SD) — соответствует установленному в проекте принципу
  "толстый слой логики тестируем на хосте, тонкий аппаратный оркестратор — только на плате".
- `firmware/bootloader/src/protocol.{c,h}` — новое событие `status`
  (`{"type":"status","state":"..."}`) — `protocol_send_status()`. Пока используется для `installing` и
  `waiting_for_sd`; полный словарь состояний — Фаза 4.
- `firmware/bootloader/src/main.c` — переупорядочен: `bsp_button_init()` добавлен;
  `bsp_usb_cdc_init()` поднимается до `sd_update_check()`/`boot_select_and_jump()` (см. решение о CDC
  выше); один комбинированный цикл ожидания вместо прежних двух (ожидание CDC / серв-цикл) — совмещает
  CDC ping/pong, LED (`LED_APP`, если CDC готов, иначе мигание `LED_HEARTBEAT`) и периодический
  пере-скан SD с повторным `boot_select_and_jump()`, если что-то установилось.
- `firmware/bootloader/CMakeLists.txt` — добавлены `bsp_button`, `bootloader_fatfs`,
  `src/update_policy.c`, `src/slot_version.c`, `src/sd_update.c`.
- **Host-тесты**: `just build::test-host` — **15/15 таргетов зелёных** (13 существовавших до Фазы 3 +
  новые `test_update_policy` [12 тестов] и `test_slot_version` [4 теста]).
- **ARM-сборка**: `just build::build-bootloader-debug` и `hab-bootloader-debug` — оба зелёные.
  `m_text` — 87928 Б из 247 КБ бюджета (34.76%, был 20.05% после Фазы 2 — рост за счёт FatFS+SDMMC,
  запас всё ещё больше половины).

### Найденный и исправленный пробел: форс. даунгрейд не имел бы эффекта

При проектировании чек-листа ниже (сценарий 3) обнаружилось: `update_policy_decide()` изначально
только писала более старый образ в неактивный слот, но **не трогала прежний активный** — а `boot_go()`
всегда выбирает более высокую версию среди валидных слотов. Значит, форс. даунгрейд физически
записался бы на flash, но реально не загрузился бы, пока прежний (более новый) активный слот не станет
невалидным сам по себе — не то поведение, которое ожидает технолог, держащий кнопку.

**Исправлено**: `update_policy_result_t` получила поле `erase_previous_active` (`true` только для
форс. даунгрейда — не для обычного "кандидат новее", там прежний активный и так проиграет сравнение
версий естественным путём). `sd_update.c` стирает прежний активный слот **только после** того, как
`slot_version_get()` подтвердил валидность только что установленного образа — на диске никогда не
бывает нуля рабочих слотов даже на середине операции. Регрессионный тест
`test_forced_downgrade_targets_and_erases_the_higher_version_slot` — зелёный.

### Найден и исправлен баг CI (не связан с Фазой 3 по сути, но всплыл при её работе)

`test-host-release` в GitHub Actions падал на компиляции вендоренного
`sdk/middleware/mcuboot_opensource/boot/bootutil/src/fault_injection_hardening.c`:
`fih_panic_loop()` использует inline-asm `__asm volatile("b fih_panic_loop")` — валидная мнемоника
только для ARM/Thumb. На x86_64-раннере GitHub Actions ассемблер падает
(`invalid instruction mnemonic 'b'`); в devcontainer на arm64 та же мнемоника случайно ассемблируется
(AArch64 тоже использует `b <label>`), маскируя проблему — отсюда расхождение "в devcontainer всё
хорошо". Исправлено точечным патчем вендоренного файла: `__asm` под `#if defined(__arm__)` (реальный
ARM-таргет — без изменений), иначе (host, любая архитектура) — портируемый `for (;;) {}`. Подтверждено
кросс-компиляцией под `-target x86_64-unknown-linux-gnu` (0 ошибок после фикса, была именно эта ошибка
до него).

### Найден и исправлен баг чек-листа (не бутлоадера): сценарий 1 не запускал установленный образ

При аппаратной ре-верификации раунда 4 + watchdog (2026-07-13) сценарий 1 чек-листа
(HARDWARE_VERIFICATION_PHASE3.md) перестал проходить: консоль показывала `waiting_for_sd` →
`installing`, прыжок доходил до `p_vt->reset()` (подтверждено в отладчике), но установленный образ
не запускался — ни сразу, ни после power cycle; erase Slot A возвращал bootloader в обычный
ping/pong-цикл.

**Причина — не регресс кода.** Стабы `test_stub` не PIC: `stub_a_*` линкован под адрес Slot A
(`0x60040000`), `stub_b_*` — под Slot Б (`0x60240000`), абсолютные адреса зашиты в бинарь при
линковке (тот же открытый вопрос, что и в Фазе 2 — "разный адрес — намеренно... открытый вопрос из
BOOTLOADER_FLASH_MAP.md §4"). Чек-лист сценария 1 предписывал класть на SD `stub_b_v2_confirmed.bin`,
а целевой слот на чистой плате (нет активного слота) по `update_policy_decide()` всегда Slot A —
несовпадение линковки и адреса установки. `slot_version_get()` (крипто-гейт) это не ловит: подпись
валидна для собственного содержимого файла независимо от того, где физически лежит слот; `boot_go()`
корректно выбирает и прыгает — исполняется код, рассчитанный на чужой адрес, без какой-либо ошибки на
CDC. Сценарии 2/3 эту грань не задевали случайно: там кандидат на понижение — всегда `stub_a`, а
целевой слот в этих сценариях (Slot Б уже активен) тоже всегда А — адрес и линковка совпадают
по совпадению постановки, не по общему принципу.

Тот же класс несовпадения — уже задокументированный в проде gap одного `TFT_APP.BIN`
([docs/mimxrt1052/UPDATE_FLOW.md](../../docs/mimxrt1052/UPDATE_FLOW.md)): production-релиз собирается
как два линкованных под разные слоты бинаря на версию, а текущий `sd_update` полагается на единый
файл. Раунд 4 просто нащупал эту границу руками на стенде, а не создал новую.

**Исправлено** — только чек-лист (не код): сценарий 1 теперь кладёт на SD `stub_a_v1_confirmed.bin`
(совпадает с гарантированным целевым слотом А), ожидаемая частота `LED_APP` — ~1/сек (v1), не ~2/сек.
`HARDWARE_VERIFICATION_PHASE3.md` §5 дополнен явным предупреждением о требовании
линковка-под-слот-установки для любого файла на SD.

### ✅ Реализовано (2026-07-10): аппаратный watchdog

**Статус:** реализован, собран (ARM + host зелёные), **аппаратно ещё не проверен** — провокацию
реального зависания и подтверждение восстановления см. в
[test_stub/HARDWARE_VERIFICATION_PHASE3.md](test_stub/HARDWARE_VERIFICATION_PHASE3.md), раздел
«Watchdog». Реализация против плана ниже:

- **Модуль `bsp/wdog`** (WDOG1): `bsp_wdog_init(timeout_s)` / `bsp_wdog_refresh()` /
  `bsp_wdog_caused_last_reset()` / `bsp_wdog_is_armed()` / `bsp_wdog_timeout_s()`. Таймаут **10 c**
  (не 5-8 — самый долгий накормленный участок оказался блочным стиранием 2 МБ ~4.8 c плюс запас;
  refresh теперь и внутри цикла стирания). `enableDebug=false` — под SWD-отладкой watchdog
  приостановлен (иначе пошаговая отладка невозможна).
- **Взвод** — в `main.c` сразу после `board_hw_init()`.
- **Кормление** — верх главного цикла; поблочно/посекторно внутри `flash_area_erase()`
  (`mcuboot_port/flash_map_backend.c`); на каждый чанк в `erase_and_copy_candidate()`
  (`sd_update.c`); один раз прямо перед `boot_select_and_jump()`. **Не** кормим перед
  `f_mount`/`SD_Init` — защита именно от них сохранена.
- **⚠️ Контракт handoff:** `WDE` (enable) — **write-once**, watchdog нельзя выключить и он переживает
  прыжок. Поэтому **каждый образ после прыжка обязан кормить watchdog** (`tft_app` в проде;
  `test_stub` уже кормит — иначе reset-loop и регрессия сценариев Фаз 2/3). Программный watchdog тут
  невозможен: зависания — внутри вендоренного `do{}while(true)`, управление к нам не возвращается.
- **Статус по USB-CDC:** событие `{"type":"wdog","armed":..,"timeout_s":..,"recovered":..}` —
  эмитится на старте, если предыдущий сброс был по watchdog, и по команде `{"type":"cmd","cmd":"wdog"}`.
  `recovered:true` = плата восстановилась после зависания.
- Ресет по watchdog безопасен по построению (ни один слот не стирается до подтверждения валидности
  нового образа — `erase_previous_active`): в худшем случае откат к «начать сессию заново».

<details><summary>Исходный план (до реализации)</summary>

Найдено при аппаратной верификации (см. [DEBUG_LOG_PHASE3_SD.md](DEBUG_LOG_PHASE3_SD.md)): у
`SD_PollingCardInsert()` (SDK, вызывается изнутри `SD_Init()`, который вызывается изнутри
`f_mount()`) при типе детекта `kSD_DetectCardByGpioCD` **нет тайм-аута вообще** — это `do {...}
while (true)` без выхода по времени, если callback `cardDetected()` ни разу не вернёт ожидаемое
для текущего условия ожидания значение. На реальном железе уже дважды пойман сценарий, когда
исполнение зависает внутри вендоренного SD/USDHC-стека (не только в этой функции — второй найденный
зависон, в `SD_ReadStatus`/`OSA_SemaphoreWait`, тоже блокирующий и без видимого тайм-аута) — то есть
риск не ограничивается одной конкретной причиной, которую можно точечно пропатчить, а системный:
любой blocking-вызов в SD/USDHC-стеке потенциально способен подвесить весь bootloader навсегда.

Для загрузчика это неприемлемо: зависание до `boot_select_and_jump()` означает, что даже валидный
образ в Slot A никогда не загрузится без внешнего вмешательства (SWD-сброс/перезагрузка вручную) —
на производстве это означает бракованную единицу без возможности восстановления штатными средствами.

**Решение — аппаратный WDOG (WDOG1 или WDOG2, MIMXRT1052)**:
- Инициализация с разумным тайм-аутом (ориентир 5-8 с — с запасом над самой длинной легитимной
  операцией, потоковым копированием чанка 4 КБ SD→flash) как можно раньше в `main.c`, после
  `board_hw_init()`.
- "Кормление" (`WDOG_Refresh`/эквивалент) — только в точках, где мы **уверены**, что прогресс
  реален: начало каждой итерации главного цикла `main.c`, и внутри `erase_and_copy_candidate()`
  (`sd_update.c`) на каждой итерации цикла копирования чанков (там уже есть `bsp_usb_cdc_poll()` —
  рядом). **Не кормить** непосредственно перед вызовом функций SDK, которые могут заблокироваться
  бесконечно (`f_mount`, `SD_Init` и всё, что внутри) — иначе watchdog перестаёт быть защитой именно
  от них.
- Ресет по watchdog безопасен по построению: ни один слот не стирается до подтверждения валидности
  только что записанного образа (см. `erase_previous_active` выше) — WDOG-ресет в худшем случае
  просто откатывает к состоянию "начать сессию заново", как обычный power cycle.
- Не реализовано в рамках текущей сессии — статус явно "запланировано", реализация и аппаратная
  проверка (спровоцировать реальное зависание и убедиться, что WDOG восстанавливает работу) —
  следующий шаг после того, как будет закрыт текущий SD/card-detect баг (иначе watchdog будет
  маскировать/сбрасывать симптом на каждом цикле, не давая найти первопричину).

</details>

**Верификация (первая часть Фазы 3, требующая реального железа — ещё не проведена)**:
1. Чистая плата (только bootloader, оба слота пустые) + SD с валидным подписанным образом →
   автоустановка, переход к загрузке (проверить по CDC-статусам и LED).
2. То же SD с образом версии ниже уже установленной, кнопка не нажата → отклонён
   (`status: update_skipped` на CDC).
3. То же, кнопка `BSP_BUTTON_1` удержана при старте → установлен И загружен (прежний активный слот
   стирается автоматически — см. находку выше).
4. SD с образом без подписи/битым TLV → отклонён (`error: SD_CANDIDATE_INVALID` или
   `SD_INSTALL_REJECTED`, в зависимости от того, где именно битый образ — в заголовке или в TLV/подписи),
   плата остаётся на последнем валидном слоте (если был) или продолжает ждать (если не было).
5. Сценарий 2 (бандл залит сразу в Slot A через SWD/SDP на этапе тестирования): SD не участвует,
   bootloader должен просто загрузить Slot A без обращения к SD-логике вообще.

Понадобится тестовый образ на SD — переиспользовать `test_stub` из Фазы 2 (`stub_a_v1_confirmed.bin`
и т.п., уже собираются `just build::build-mcuboot-stub`), переименованный/скопированный в
`TFT_APP.BIN`, а не дожидаться реального `tft_app`. Точный порядок команд — см. чат/раздел ниже
(зависит от того, что именно уже стоит на конкретной тестовой плате).

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

## Фаза 6 — Устойчивость и восстановление (recovery)

**Рекомендованный порядок исполнения: раньше Фаз 4/5** (см. статус-таблицу). Ядро (boot_select,
sd_update, update_policy, watchdog) свежее в контексте; закрывает уже случившуюся на практике боль —
окирпичивание платы зависшим образом. Полностью реализуема и железно проверяема уже сейчас через
`test_stub` (сторона tft_app — контракт, документируется в 6c под будущий план tft_app).

**Цель**: гарантировать, что ни зависший в рантайме образ, ни кривая установка не превращают полевую
плату в кирпич, и дать оператору ручной аварийный вход. Аппаратный watchdog (Фаза 3) уже ловит
зависание — Фаза 6 надстраивает над ним **логику восстановления**: что делать ПОСЛЕ того, как
watchdog сбросил плату.

### Таксономия отказов (что чем закрывается)

| # | Сценарий | Механизм | Статус |
|---|---|---|---|
| A | Новый образ завис ДО подтверждения себя | watchdog-сброс + штатный revert MCUboot (`boot_select_or_erase`, [loader.c](../../sdk/middleware/mcuboot_opensource/boot/bootutil/src/loader.c) — стирает `copy_done==SET && image_ok!=SET`) → откат на прежний слот | **авто, кода в загрузчике не требует** (нужен лишь отложенный confirm в tft_app, 6c) |
| B | **Подтверждённый** образ завис в рантайме | счётчик watchdog-сбросов подряд + фолбэк (6a) | новый код 6a |
| C | Откатываться некуда (единственный/оба слота зависают) | recovery-режим: подавить загрузку, ждать SD (6b) | новый код 6b |
| D | Оператор хочет чистый старт вручную | recovery-режим по BTN_2 (6b) | новый код 6b |

Ключ к классу A — **отложенное подтверждение**: если tft_app зовёт `boot_set_confirmed()` только
доказав здоровье (проработал T c, self-check пройден), любой битый новый образ, зависший в этом
окне, остаётся `image_ok!=SET` и откатывается MCUboot'ом автоматически. Самый частый класс закрыт
без единой строки в загрузчике.

### Решения, принятые в обсуждении Фазы 6 (не пересматриваются)

- **Отложенное подтверждение** (`boot_set_confirmed()` после доказанного здоровья) — контракт
  tft_app. Переводит класс A в авто-откат.
- **WDOG переживает прыжок** (Фаза 3, `WDE` write-once) + **alive-flag идиом** в tft_app: главный
  цикл ставит `alive=true`, таймерный ISR раз в ~1 c кормит watchdog только если `alive` был
  выставлен (иначе главный поток завис → watchdog сработает). Одна строка в цикле, ловит зависания
  главного потока, а не только полный локап. Таймаута 10 c хватает любому нормальному циклу с
  запасом ×1000; отдельного внимания требуют только единичные блокирующие операции > таймаута.
- **Класс B — счётчик watchdog-сбросов подряд**, хранится в **retained-регистре `SRC_GPR`**
  (переживает тёплый/watchdog-сброс, обнуляется на POR). Порог **настраиваемый `#define`, дефолт 3**.
- **Фолбэк класса B — Вариант A (стереть зависший слот)**: при достижении порога, ЕСЛИ второй слот
  валиден (`slot_version_get`) → стереть активный (зависший) слот → `boot_go()` сам выберет второй →
  прыжок. Если валидного второго слота НЕТ → **не стирать единственный образ** (иначе ноль рабочих
  слотов) → recovery-режим.
- **Бан зависшего слота — per-session (Вариант 1)**: отдельного флеш-маркера НЕТ; «забанен» = «счётчик
  ≥ порога». На POR счётчик обнуляется → на холодном старте зависший слот пробуется снова (даёт шанс,
  если завис был транзиентным). Детерминированный завис одинокого слота → оператор жмёт **BTN_2**
  (немедленный recovery), не дожидаясь порог×10 c. Флеш-маркер сознательно не делаем — если позже в
  поле детерминированные зависы одинокого слота окажутся частыми, добавим как отдельный кусок (место
  в дизайне оставлено).
- **Recovery-режим — единое состояние, два триггера**: авто (класс C — порог без валидного фолбэка)
  и ручной (BTN_2 удержан при старте). Поведение: **прыжок подавлен** (это само рвёт loop), отдельный
  LED-паттерн, CDC `recovery_mode`, ждём SD, **version-gate ослаблен** (принимаем любую подписанную
  версию без кнопки — подпись проверяется всегда).
- **Стирание в recovery — отложенное** (на момент установки, не на входе): вход в recovery ничего не
  стирает → случайное удержание BTN_2 безопасно (без SD-образа ничего не теряется); при найденном
  валидном подписанном образе — стереть оба слота и поставить свежий в Slot A (sd_update и так стирает
  целевой слот). «Чистый борт» достигается ровно тогда, когда есть чем заменить.
- **После recovery-установки — авто-прыжок** в свежепровалидированный образ (если и он зависнет —
  отработает обычный класс A/B).
- **Приоритет триггеров при старте**: BTN_2 (recovery) проверяется ПЕРВЫМ — если удержан, в обычный
  boot-путь не идём вообще. Затем BTN_1 (даунгрейд) в обычном пути. Recovery «главнее».
- **Правила обнуления счётчика**: (1) POR; (2) успешная установка нового образа (SD/recovery — свежему
  образу полный бюджет попыток); (3) фолбэк-стирание (ситуация изменилась); (4) health-mark работающим
  образом.

### Честная граница (что НЕ восстанавливается автоматически — задокументировать)

Если образ стабильно работает, **обнуляет счётчик** (health-mark), и лишь ПОТОМ ловит редкий баг
(конкретный файл на SD, конкретное CAN-сообщение) — счётчик каждый раз обнуляется до зависания,
автопорог не копится, loop автоматически не ловится. Это фундаментально (по таймеру не отличить
«здоров» от «здоров, но потом словил редкое»). Для такого класса: плата видимо циклится (watchdog +
recovery-LED/CDC это показывают), лечится SD-фиксом или BTN_2. Watchdog как минимум не даёт «намертво»
зависнуть. Принимаем и пишем в доке, не делаем вид, что решаем всё.

### Разбивка

**6a — Класс B: счётчик watchdog-сбросов + фолбэк A** (сторона загрузчика, host-тестируема)
- Расширить `bsp/wdog` (или новый `bsp/boot_state`): счётчик в `SRC_GPR[n]` (выбрать свободный —
  `GPR1/2` заняты ROM под warm-boot entry/arg, `GPR10` под redundant/secondary boot; кандидаты
  `GPR5..8`, **проверить по RM/на железе**, что переживает watchdog-сброс и что мы корректно обнуляем
  на POR через `SRC->SRSR`). API: `bsp_boot_attempt_count()`, `bsp_boot_attempt_inc()`,
  `bsp_boot_attempt_reset()`, `bsp_boot_health_mark()` (= reset, зовётся приложением).
- **Чистая host-тестируемая функция решения** (по образцу `update_policy_decide`), напр.
  `recovery_decide(is_por, attempt_count, threshold, slot_a_state, slot_b_state, btn2_held)` →
  `{NORMAL_BOOT | ERASE_ACTIVE_THEN_BOOT_OTHER(active_id) | RECOVERY}`. Активный слот вычисляется из
  `slot_version_get()` обоих слотов (тот же приём, что уже в `update_policy`). Тесты в
  `tests/host/recovery/` — все ветки таблицы таксономии.
- `main.c`: до `boot_select_and_jump()` — прочитать причину сброса (POR→reset счётчика), применить
  `recovery_decide`. Ветки: NORMAL → `attempt_inc()` + прыжок; ERASE_OTHER → стереть активный слот
  (`flash_area_erase`) + reset счётчика + прыжок (boot_go выберет оставшийся); RECOVERY → в состояние
  6b. **Инкремент — перед прыжком** (если образ зависнет, следующий старт увидит инкремент; здоровый
  образ обнулит через health-mark).
- Расширить CDC `wdog`-статус: `{"type":"wdog",...,"reset_count":N,"threshold":M}` — диагностика в
  поле («сбрасывалась N из M»).

**6b — Recovery-режим + ручной BTN_2** (тонкий аппаратный оркестратор)
- Единое состояние recovery: подавить `boot_select_and_jump()`, отдельный LED-паттерн (узнаваемый
  «шиммер»/SOS обоими LED, явно отличный от heartbeat 50/450 и app 500/250 — сведётся в словарь
  Фазы 4), CDC `status: recovery_mode`.
- BTN_2 (`BSP_BUTTON_2`, свободен): ранний сэмпл + защёлкивание (как BTN_1 в Фазе 3, item 2),
  приоритет над обычным boot-путём и над BTN_1.
- SD-скан с **ослабленным version-gate** (в `update_policy` — флаг `recovery`/`force_install`:
  принять любую подписанную версию без кнопки; подпись/крипто-гейт остаются). Отложенное стирание:
  на валидном образе — стереть оба слота, поставить в Slot A, авто-прыжок.
- Кормление watchdog в recovery — как везде (верх цикла + per-block при стирании; 2×2 МБ ~10 c
  накрыты поблочным refresh).

**6c — Контракт tft_app** (документация под будущий план tft_app, кода в загрузчике нет)
- tft_app ОБЯЗАН: (1) обслуживать watchdog (alive-flag идиом); (2) `boot_set_confirmed()` —
  отложенно, после доказанного здоровья; (3) `bsp_boot_health_mark()` — дойдя до устойчивого
  состояния (можно раньше confirm). (2) и (3) можно разнести: health-mark пораньше (дошёл до главного
  цикла), confirm позже (полный self-test).
- Опционально: tft_app может переконфигурировать таймаут WDOG (`WCR.WT` переписываем, `WDE` — нет)
  под свои длинные операции.

### Стенд (доработки `test_stub`)

Опции компиляции/поведения заглушки, чтобы прогнать все классы на железе:
- confirm: сразу / отложенно (через N c) / никогда;
- зависнуть: никогда / до health-mark / после health-mark / после confirm;
- обслуживать watchdog: да / нет.

### Верификация (реальное железо — чек-лист, детали в отдельном HARDWARE_VERIFICATION при реализации)

1. **Класс A** — свежий образ, `confirm=никогда`, `hang=после jump`: watchdog-сброс → MCUboot
   стирает → откат на прежний слот (или wait_for_sd, если прежнего нет).
2. **Класс B, есть фолбэк** — `confirm=сразу`, `hang=после confirm`, второй слот валиден: после
   порога — зависший слот стёрт, загрузка второго; CDC `reset_count` растёт до порога.
3. **Класс B, нет фолбэка** — то же, но второй слот пуст: после порога — recovery-режим (LED-шиммер,
   CDC `recovery_mode`), плата не циклится дальше.
4. **Recovery по BTN_2** — валидный образ в слоте, удержать BTN_2 при старте → recovery-режим без
   загрузки; вставить SD с образом → стирание обоих + установка + авто-прыжок.
5. **Транзиент (per-session)** — после класса B без фолбэка (recovery) сделать power cycle БЕЗ BTN_2
   → счётчик обнулён (POR), слот пробуется снова (подтверждает per-session-семантику).
6. **Не мешает норме** — прогнать сценарии Фазы 3 как есть: без ложных recovery/сбросов.

### Открытые вопросы к реализации

- `SRC_GPR[n]`: индекс (свободный от ROM) + подтвердить переживание watchdog-сброса и обнуление на
  POR (RM + железо).
- `SRC->SRSR` — корректно читать/чистить (w1c) причину сброса, различать POR vs WDOG (биты
  `SRC_SRSR_WDOG_RST_B` / POR).
- Word choice recovery-LED — согласовать со словарём Фазы 4, чтобы не переделывать.

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
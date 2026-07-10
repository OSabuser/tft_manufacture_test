# test_slot_version

## Модуль под тестом

`firmware/bootloader/src/slot_version.c` (`slot_version.h`) — read-only
«пик» версии образа в слоте bootutil (Фаза 3), без побочных эффектов на
flash: в отличие от `boot_go()`, не проверяет и не изменяет статус
confirm/copy_done.

## Категория

B — интеграционный host-тест: реальный `bootutil`
(`bootutil_img_validate()`, полная проверка hash + ECDSA-подписи)
линкуется поверх фейкового flash-бэкенда вместо `bsp_qspi_flash`.
Переиспользует фейковый backend и фикстуры из `tests/host/mcuboot_port/` —
те же, что и `test_mcuboot_boot_select`.

## Моки

- `fake_flash_map_backend.c/.h` (переиспользуется из `mcuboot_port/`) —
  тестовая реализация контракта `flash_map.h` поверх памяти хоста вместо
  `bsp_qspi_flash`. Не fff-мок — полноценная тестовая реализация backend'а.
- `host_link_shims.c` (переиспользуется из `mcuboot_port/`) — заглушки
  символов, недостающих только при линковке `bootutil` на хосте; не
  участвуют в тестируемой логике.
- `fixtures/*.bin` (общие с `test_mcuboot_boot_select`) — реальные,
  подписанные `imgtool` тестовым ключом MCUboot образы (`valid_v1`,
  `corrupt_v1`, `valid_v2_unconfirmed`).

## Что проверяется

- **Валидный слот** — `slot_version_get()` возвращает `true` и версию,
  совпадающую с версией в заголовке фикстуры (major/minor/revision).
- **Повреждённый слот** (битый хэш/подпись) — возвращает `false`.
- **Пустой слот** (стёрт в `0xFF`, magic не совпадает с `IMAGE_MAGIC`) —
  возвращает `false`.
- **Неподтверждённый, но валидный образ** (`valid_v2_unconfirmed.bin`) —
  версия всё равно читается, и повторный вызов `slot_version_get()` на том
  же слоте по-прежнему успешен — слот не «стирается», как это сделал бы
  повторный `boot_go()`.

## Гарантии

- `slot_version_get()` не имеет побочных эффектов на flash — не читает и
  не изменяет статус confirm/copy_done, поэтому безопасен для
  многократного вызова и вызова до первого `boot_go()`.
- Валидация полная — тот же путь `bootutil_img_validate()`, что
  использует `loader.c` для каждого слота при штатной загрузке, а не
  упрощённая проверка одного заголовка.
- Идемпотентность: повторные вызовы для одного и того же
  неподтверждённого образа дают одинаковый результат — в отличие от
  `boot_go()`, который стирает неподтверждённый образ при повторном
  вызове (см. `test_mcuboot_boot_select::test_boot_go_reverts_unconfirmed_image`).

## Запуск

```bash
ctest --preset host-debug-test -R test_slot_version -V
```

Путь к фикстурам (`FIXTURES_DIR`) общий с `test_mcuboot_boot_select` и
прокидывается автоматически через `tests/host/CMakeLists.txt`.

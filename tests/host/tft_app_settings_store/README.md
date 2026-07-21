# test_tft_app_settings_store

## Модуль под тестом

`firmware/tft_app/src/services/settings_store/src/settings_codec.c` — **чистая**
сериализация ядра настроек (страница `magic`/`version`/`CRC32`, дефолты).
Flash-адаптер `settings_store.c` (load/save через `bsp_qspi_flash`) — не здесь,
он проверяется на железе (HIL).

## Категория

A — платформонезависимый. `bsp/qspi_flash.h` подключается только ради
геометрии (`BSP_QSPI_SECTOR_SIZE`) — это чистый заголовок без SDK, моки не нужны.

## Что проверяется

- Размер `settings_page_t` — ровно один сектор QSPI (дублирует `_Static_assert`).
- Дефолты вменяемы (протокол NKU, логи вкл, громкости, адрес `proto_slice[0]`=0).
- `serialize` проставляет `magic` (`STFT`) и `version`.
- **Round-trip**: `serialize`→`deserialize` сохраняет поля (вес, вместимость, год,
  адрес, тумблер логов, серийник).
- **Отбраковка**: битый `magic` / несовпадающая `version` / испорченный байт
  данных (расходится CRC) → `deserialize` возвращает false.
- Свежестёртая страница (вся `0xFF`) → невалидна.

## Гарантии

- CRC32 считается по всем байтам страницы, кроме самого поля `crc32`.
- Любая невалидность (`magic`/`version`/`CRC`) → false; вызывающий (`settings_store.c`)
  подставляет дефолты, устройство не «кирпичится».

## Запуск

```bash
ctest --preset host-debug-test -R test_tft_app_settings_store -V
```

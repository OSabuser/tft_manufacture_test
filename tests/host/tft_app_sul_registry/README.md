# test_sul_registry

## Модуль под тестом

`firmware/tft_app/src/domain/sul/src/sul_registry.c` — реестр драйверов СУЛ:
выбор активного протокола (ARCH.md §8, Фаза 3.3).

## Категория

A — платформонезависимый. Реестр — статические данные + чистые функции
поиска/выбора, HAL не трогает.

## Что проверяется

- `sul_registry_active()` — дефолт (до первого `set_active()`) и после явного
  выбора известного id.
- `sul_registry_find()` — `NULL` на неизвестный id.
- `sul_registry_set_active()` — известный id переключает активный драйвер;
  неизвестный id **игнорируется** (активный не меняется) — защита от мусора
  в `settings_device_t.protocol_id`.
- `sul_registry_count()` — количество зарегистрированных протоколов.
- Дескриптор настроек НКУ-CAN (`sul_settings_desc_t`) присутствует и содержит
  ожидаемый диапазон адреса (0..15).

## Запуск

```bash
ctest --preset host-debug-test -R test_sul_registry -V
```

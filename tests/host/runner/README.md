# test_firmware_runner

## Модуль под тестом

`firmware/test/src/test_runner.c` — state machine выполнения реестра
тестовых модулей.

## Категория

B — зависит от `bsp_tick_get_ms`, `bsp_delay`, `bsp_usb_cdc_poll`,
`cli_process`, `protocol_send_*`, подменяемых через fff.

## Моки

`bsp_tick_get_ms`, `bsp_delay`, `bsp_usb_cdc_poll`, `cli_process`,
`protocol_send_test_begin`, `protocol_send_test_result`,
`protocol_send_summary`, `protocol_send_confirm_request`,
`protocol_send_error`, `protocol_send_pong`, `protocol_send_session_start`,
`protocol_send_test_list`. UNIT_TEST seam: реестр тестовых модулей
(`g_unit_test_registry` / `g_unit_test_registry_size`) подставляется этим
тестовым файлом вместо реального реестра прошивки — `set_registry()`
меняет состав между тестами (сборка выполняется с `-DUNIT_TEST`). Кастомный
`custom_fake` `capture_test_result` копирует `test_result_t` по значению,
т.к. `protocol_send_test_result` получает указатель на стековую переменную
`execute_test()`.

## Что проверяется

- **Базовое состояние** — runner не занят изначально; `run_all` на пустом
  реестре сразу шлёт summary с нулевыми счётчиками; `run_single` с
  неизвестным id шлёт `UNKNOWN_TEST`.
- **Одиночный запуск** — успешный тест шлёт `test_begin` + `test_result` и
  освобождает runner; проваленный тест шлёт результат со статусом FAIL.
- **run_all** — несколько успешных тестов дают верный summary;
  критический (`critical=true`) провал прерывает выполнение — оставшиеся
  тесты получают статус SKIP, а `summary` отражает pass/fail/skip и
  итоговый `overall`.
- **pre_confirm state machine** — модуль с `pre_confirm_prompt` переводит
  runner в состояние ожидания (busy) и шлёт `confirm_request`, не запуская
  тест; повторный запуск во время ожидания получает ошибку `BUSY`;
  `on_confirm(id, true)` выполняет тест; `on_confirm(id, false)` помечает
  тест как SKIP; истечение `PROTOCOL_CONFIRM_TIMEOUT_MS` без ответа тоже
  даёт SKIP; подтверждение с чужим id игнорируется как устаревшее (stale).

## Гарантии

- Критический провал теста в `run_all` прерывает выполнение остальных —
  они получают статус SKIP, а не PASS/FAIL.
- Runner не принимает новый запуск, пока занят (`BUSY`), в частности во
  время ожидания pre-confirm.
- Тест с pre-confirm не выполняется без явного положительного
  подтверждения: отказ, таймаут ожидания или подтверждение с чужим id не
  приводят к выполнению теста.
- `summary` всегда отражает фактические счётчики passed/failed/skipped и
  корректный итоговый `overall`.

## Запуск

```bash
ctest --preset host-debug-test -R test_firmware_runner -V
```

# test_protocol

## Модуль под тестом

`firmware/test/src/protocol.c` — сериализация JSON-событий протокола
(`session_start`, `test_begin`, `test_result`, `summary`, `confirm_request`,
`pong`, `error`).

## Категория

B — зависит от `cli_send()` и `bsp_tick_get_ms()`, подменяемых через fff.

## Моки

- `cli_send` — fff void-фейк; итоговая строка захватывается через
  `custom_fake` (`capture_cli_send`) в статический буфер, т.к. аргумент —
  указатель на стековый буфер `protocol_send_*()`, живой только до возврата.
- `bsp_tick_get_ms` — fff value-фейк, управляет значением `uptime_ms` в
  `session_start`.
- `bsp_delay` — fff void-фейк-заглушка (не используется в проверках).

## Что проверяется

Точный JSON-текст, передаваемый в `cli_send()`, для каждого типа сообщения:
`session_start` (нулевой и ненулевой uptime, версия прошивки), `test_begin`
(`critical: true/false`), `test_result` (`pass`, `fail` с деталями, `skip`),
`summary` (общий результат pass/fail), `confirm_request` (кастомный таймаут
и подстановка таймаута по умолчанию при `timeout_ms == 0`), `pong`, `error`
(разные коды ошибок).

## Гарантии

- Формат JSON точно соответствует протоколу — сравнивается побайтово с
  эталонной строкой, а не только наличием отдельных полей.
- Каждый `protocol_send_*()` вызывает `cli_send()` ровно один раз.
- `confirm_request` с `timeout_ms == 0` всегда получает
  `PROTOCOL_CONFIRM_TIMEOUT_MS` вместо нуля.

## Запуск

```bash
ctest --preset host-debug-test -R test_protocol -V
```

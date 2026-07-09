# test_cli

## Модуль под тестом

`firmware/test/src/cli.c` — построчный разбор JSON-протокола от USB CDC и
диспетчеризация команд.

## Категория

B — зависит от `bsp_usb_cdc`, `protocol_send_*`, `test_runner_*`,
`bsp_prov_read_uid`, подменяемых через fff.

## Моки

`bsp_usb_cdc_write`/`bsp_usb_cdc_read` (чтение эмулируется хелпером
`inject()`, который наполняет внутренний буфер «как будто USB» и вызывает
`cli_process()`), `protocol_send_pong`, `protocol_send_error`,
`protocol_send_uid_response`, `protocol_send_version_response`,
`test_runner_run_all`, `test_runner_run_single`, `test_runner_run_selected`,
`test_runner_on_confirm`, `test_runner_send_list`, `bsp_prov_read_uid`.
Кастомные `custom_fake` (`capture_run_single`, `capture_on_confirm`,
`capture_uid_response`, `capture_run_selected`) копируют строковые
аргументы по значению, т.к. `cli.c` передаёt указатели на собственные
локальные буферы, которые становятся dangling после возврата
`cli_process()`.

## Что проверяется

- **Команды** (`type: cmd`) — `ping`→pong, `run_all`→запуск всех,
  `run`+`id`→запуск одного с передачей id, `get_uid` (успех и ошибка
  чтения UID), `get_version`, `list_tests`, `run_selected` с массивом id.
- **Подтверждения** (`type: confirm`) — диспетчеризация `confirmed:
  true/false` с корректным id.
- **Ошибки протокола** — отсутствует `type` или обязательное поле
  (`id`/`confirmed`/`tests`) → `PARSE_ERR`; неизвестный `type`/`cmd` →
  `UNKNOWN_CMD`; строка длиннее `CLI_LINE_BUF_SIZE` → `LINE_TOO_LONG`.
- **Построчный ввод** — пустая строка игнорируется, `\r\n` обрабатывается
  как `\n`, две команды в одном чтении диспетчеризуются обе по отдельности.

## Гарантии

- Каждая валидная команда приводит ровно к одному вызову соответствующего
  обработчика (`test_runner_*`/`protocol_send_*`).
- Любая ошибка парсинга или неизвестная команда отправляет ровно один
  `protocol_send_error()` с точным, стабильным кодом ошибки.
- Построчный парсер корректно разделяет несколько команд в одном чтении и
  не путает `\r\n` и `\n`.
- Переполнение буфера строки не приводит к падению — возвращается
  контролируемая ошибка `LINE_TOO_LONG`.

## Запуск

```bash
ctest --preset host-debug-test -R test_cli -V
```

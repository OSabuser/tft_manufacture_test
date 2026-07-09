# uart_host_mock_example

## Модуль под тестом

Не сам `bsp/uart_host/src/uart_host.c` (он верифицируется отдельными
HIL-тестами через pyserial на реальном железе), а корректность вызова его
публичного API (`bsp/uart_host/include/bsp/uart_host.h`) кодом верхнего
уровня — через готовый мок `bsp/uart_host/mocks/uart_host_mock.c`.

## Категория

B — зависит от API `bsp_uart_host`, подменяемого через fff.

## Моки

Полный fff-мок API `bsp_uart_host`, поставляемый модулем
`bsp/uart_host/mocks/uart_host_mock.{c,h}`:
`bsp_uart_host_init`, `bsp_uart_host_deinit`, `bsp_uart_host_write`,
`bsp_uart_host_write_str`, `bsp_uart_host_read`, `bsp_uart_host_read_byte`,
`bsp_uart_host_rx_available`, `bsp_uart_host_rx_flush`. Сброс всех фейков —
макросом `UART_HOST_MOCK_RESET_ALL()` в `setUp()`.

## Что проверяется

- **init** — baud rate передаётся без искажений, ошибка инициализации
  (`BSP_ERR_INIT`) пробрасывается наверх.
- **write / write_str** — корректные буфер/длина или строка передаются в
  API, ошибка при неинициализированном UART пробрасывается.
- **read_byte** — успешное значение, таймаут (`-1`), конвертация
  `BSP_UART_HOST_WAIT_FOREVER` → `UINT32_MAX`.
- **read** — корректные буфер/размер/таймаут передаются, частичное чтение
  (меньше запрошенного) не трактуется как ошибка.
- **rx helpers** — `rx_available` возвращает счётчик, `rx_flush`
  вызывается.
- **Сброс мока** — `UART_HOST_MOCK_RESET_ALL()` обнуляет счётчики вызовов и
  `return_val` между тестами.

## Гарантии

- Код верхнего уровня передаёт в `bsp_uart_host_*` ровно те аргументы,
  которые получил сам (baud rate, буфер, длина, таймаут).
- Коды ошибок и специальные значения (таймаут `-1`,
  `BSP_UART_HOST_WAIT_FOREVER`) не теряются и не искажаются на пути через
  API.
- Частичное чтение — штатный случай, а не ошибка.
- Между тестами мок гарантированно возвращается в чистое состояние.

## Запуск

```bash
ctest --preset host-debug-test -R uart_host_mock_example -V
```

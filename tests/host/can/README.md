# test_bsp_can

## Модуль под тестом

`bsp/can/src/can.c` + `utils/ring_buffer/ring_buffer.c` (реальный, не
мокается — используется как внутренняя зависимость `can.c`).

## Категория

B — BSP-модуль, зависит от FlexCAN SDK (`fsl_flexcan.h`, `fsl_clock.h`).
Unity + fff.

## Моки

Набор fff-фейков SDK FlexCAN: `FLEXCAN_GetDefaultConfig`,
`FLEXCAN_CalculateImprovedTimingValues`, `FLEXCAN_Init`, `FLEXCAN_Deinit`,
`FLEXCAN_SetTxMbConfig`, `FLEXCAN_SetRxMbConfig`,
`FLEXCAN_SetRxIndividualMask`, `FLEXCAN_WriteTxMb`, `FLEXCAN_ReadRxMb`,
`FLEXCAN_GetMbStatusFlags`, `FLEXCAN_ClearMbStatusFlags`, а также
`bsp_tick_get_ms` (управляемое время для тестов таймаутов) и
`CLOCK_EnableClock` (ERRATA 50235 workaround в `bsp_can_init()`).
Кастомные `custom_fake`: `read_rx_mb_inject` (подставляет заранее
подготовленный RX-фрейм), `get_mb_flags_once` (флаг готовности только на
первый вызов), `tick_advancing` (линейно растущее время для таймаутов),
`capture_tx_frame` (захват TX-фрейма для проверки конвертации ID/данных).

## Что проверяется

- **Init / Deinit** — успешная инициализация; отклонение `NULL`-конфига,
  нулевого и слишком высокого bitrate, ошибки расчёта тайминга; повторная
  инициализация вызывает `Deinit` перед новым `Init`; `deinit` без `init`
  — no-op.
- **TX** (`bsp_can_send`) — успешная отправка, `NULL`-фрейм, `dlc > 8`,
  отправка без инициализации, занятый MB (`BSP_ERR_BUSY`), таймаут
  ожидания готовности (`BSP_ERR_TIMEOUT`).
- **Фильтрация** (`bsp_can_set_filter` / `bsp_can_accept_all`) — STD/EXT
  ID, индекс фильтра вне диапазона, вызов без инициализации,
  `accept_all` настраивает минимум 2 RX MB и деактивирует ранее
  настроенные фильтры.
- **RX** (`bsp_can_receive`) — успешный приём STD/EXT фрейма, таймаут,
  `NULL`-указатель, приём без инициализации, неблокирующий опрос при
  `timeout_ms == 0`.
- **Callback-заглушка** — `bsp_can_register_rx_callback` возвращает
  `BSP_ERR_NOT_SUPPORTED`.
- **Конвертация фреймов** — корректность кодирования STD/EXT ID
  (`FLEXCAN_ID_STD`/`FLEXCAN_ID_EXT`) и порядка байт данных при переводе
  между `bsp_can_frame_t` и SDK `flexcan_frame_t`.

## Гарантии

- Все публичные функции проверяют, что модуль инициализирован, и
  возвращают `BSP_ERR_PARAM` в противном случае.
- Параметры валидируются перед обращением к SDK: `dlc <= 8`, bitrate в
  допустимом диапазоне, индекс фильтра в пределах `BSP_CAN_FILTER_MAX`.
- TX и RX корректно завершаются по таймауту (`BSP_ERR_TIMEOUT`), если MB не
  становится готовым.
- STD/EXT кодирование идентификатора и порядок байт данных не искажаются
  при конвертации между форматами приложения и SDK.
- `bsp_can_accept_all()` деактивирует ранее настроенные пользовательские
  фильтры перед установкой приёма «всех кадров».

## Запуск

```bash
ctest --preset host-debug-test -R test_bsp_can -V
```

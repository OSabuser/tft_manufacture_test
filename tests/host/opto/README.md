# test_bsp_opto

## Модуль под тестом

`bsp/opto/src/opto.c` (`bsp/opto/include/bsp/opto.h`)

## Категория

B — BSP-модуль, зависит от `fsl_gpio.h` и `bsp/tick.h`. Unity + fff.

## Моки

SDK GPIO/NVIC: `GPIO_PinInit`, `GPIO_PinWrite`, `GPIO_PinRead`,
`GPIO_SetPinInterruptConfig`, `GPIO_EnableInterrupts`,
`GPIO_DisableInterrupts`, `GPIO_GetPinsInterruptFlags`,
`GPIO_ClearPinsInterruptFlags`, `EnableIRQ`. Плюс BSP-зависимости:
`BOARD_InitRS_GPIO` (`board.h`-стаб), `bsp_tick_get_ms` (`bsp/tick.h`-стаб).
Кастомные `custom_fake`: захват конфигурации `GPIO_PinInit` и
`GPIO_SetPinInterruptConfig` по значению (пин + структура конфигурации).
ISR-обработчик `GPIO1_Combined_16_31_IRQHandler()` вызывается напрямую из
теста (`simulate_isr`) с заранее выставленными
`GPIO_GetPinsInterruptFlags`/`GPIO_PinRead` — реальное прерывание не
эмулируется, дёргается тот же код, что вызвало бы железо.

## Что проверяется

- **Инициализация** — число и параметры настраиваемых каналов в
  зависимости от `rs_as_gpio` (2 канала IN1/IN2, либо 3 с RS), направление
  всегда input, корректные номера пинов, IRQ включается на каждый канал и
  глобально, `BOARD_InitRS_GPIO()` вызывается только при `rs_as_gpio ==
  true`.
- **Начальный фронт для MODE_LEVEL** — если пин LOW (INACTIVE) при init,
  выбирается RISING; если HIGH (ACTIVE) — FALLING.
- **Переключение фронта в ISR** (MODE_LEVEL) — после RISING фронта ISR
  переключает ожидание на FALLING и обратно.
- **bsp_opto_read** — начальное состояние по уровню пина, отключённый
  RS-канал и некорректный номер канала всегда дают INACTIVE.
- **bsp_opto_process — дебаунс (MODE_LEVEL)** — коллбэк не срабатывает до
  истечения `debounce_ms`, срабатывает после с правильными
  каналом/состоянием, не срабатывает повторно если состояние не изменилось
  или уже обработано предыдущим `process()`.
- **Независимость каналов** — срабатывание одного канала не влияет на
  состояние и коллбэки другого.
- **MODE_PROTO** — коллбэк вызывается синхронно прямо в ISR (не в
  `process()`), после срабатывания IRQ канала отключается,
  `bsp_opto_process()` не генерирует для него коллбэков,
  `bsp_opto_proto_arm()` перевзводит прерывание (для PROTO) и является
  no-op для LEVEL-канала, `bsp_opto_read()` для PROTO-канала всегда
  INACTIVE.

## Гарантии

- Полярность active-HIGH: `raw=1` → `ACTIVE`, `raw=0` → `INACTIVE`.
- MODE_LEVEL: переходы состояния дебаунсятся (`debounce_ms`) и
  подтверждаются только в `bsp_opto_process()`; ISR лишь фиксирует
  «pending» и переключает направление ожидаемого фронта.
- MODE_PROTO: обрабатывается целиком в ISR, коллбэк синхронный,
  `bsp_opto_process()` его не трогает.
- Каналы полностью независимы — событие на одном не искажает состояние
  другого.
- Некорректный номер канала не приводит к падению, все геттеры
  возвращают безопасное значение по умолчанию (`INACTIVE`).

## Запуск

```bash
ctest --preset host-debug-test -R test_bsp_opto -V
```

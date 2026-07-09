# test_bsp_led

## Модуль под тестом

`bsp/led/src/led.c` (`bsp/led/include/bsp/led.h`)

## Категория

B — BSP-модуль, зависит от `fsl_gpio.h`. Unity + fff.

## Моки

- `GPIO_PinInit` — фейк, конфигурация захватывается через `custom_fake`
  (`GPIO_PinInit_capture`) и копируется по значению, т.к. `gpio_pin_config_t`
  живёт на стеке `bsp_led_init()` и становится dangling после возврата.
- `GPIO_PinWrite` — фейк, проверяется последний переданный уровень
  (`arg2_val`).

## Что проверяется

- **Инициализация** — `GPIO_PinInit` вызывается для каждого LED, пины
  настраиваются как output, начальный уровень HIGH (LED выключен), оба LED
  выключены сразу после `bsp_led_init()`.
- **on / off** — `bsp_led_on()`/`bsp_led_off()` меняют логическое состояние и
  пишут в GPIO инвертированный уровень.
- **toggle** — переключение из off→on, on→off, двойной toggle возвращает
  исходное состояние.
- **set** — `bsp_led_set(led, bool)` включает/выключает по явному флагу.
- **Независимость** — состояние `LED_HEARTBEAT` и `LED_APP` не пересекается.

## Гарантии

- Полярность active-LOW: `on` → GPIO=0, `off` → GPIO=1.
- После инициализации оба LED гарантированно выключены.
- `toggle`, применённый чётное число раз, возвращает исходное состояние.
- LED-каналы независимы друг от друга.

## Запуск

```bash
ctest --preset host-debug-test -R test_bsp_led -V
```

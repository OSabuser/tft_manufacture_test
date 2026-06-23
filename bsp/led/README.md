# bsp_led — пользовательские светодиоды

Управление двумя пользовательскими светодиодами на плате.
Используется для индикации heartbeat и состояния приложения.

---

## Аппаратура

| Идентификатор   | Сигнал   | Пин MCU       | Корпус | GPIO     | Активный уровень |
| --------------- | -------- | ------------- | ------ | -------- | ---------------- |
| `LED_HEARTBEAT` | UserLed1 | GPIO_SD_B1_03 | M4     | GPIO3[3] | LOW (0 = горит)  |
| `LED_APP`       | UserLed2 | GPIO_SD_B1_04 | P2     | GPIO3[4] | LOW (0 = горит)  |

Пины настроены в `BOARD_InitPins()` (`generated/pin_mux.c`) как OUTPUT,
`INIT_GPIO_VALUE = 1U` — оба LED выключены сразу после `bsp_led_init()`.

---

## API

```c
void bsp_led_init(void);

void bsp_led_on(led_id_t id);
void bsp_led_off(led_id_t id);
void bsp_led_toggle(led_id_t id);
void bsp_led_set(led_id_t id, bool on);
bool bsp_led_get(led_id_t id);
```

Вызвать `bsp_led_init()` один раз после `board_hw_init()`.

---

## Быстрый старт

```c
#include "bsp/led.h"

bsp_led_init();

/* heartbeat в main loop */
bsp_led_toggle(LED_HEARTBEAT);

/* индикация события */
bsp_led_on(LED_APP);
/* ... */
bsp_led_off(LED_APP);
```

---

## CMake

```cmake
target_link_libraries(firmware_test PRIVATE bsp_led)
```

**Зависимости модуля:**

| Зависимость  | Тип     | Описание                                     |
| ------------ | ------- | -------------------------------------------- |
| `bsp_status` | PUBLIC  | `bsp_status_t` в публичном API               |
| `bsp_board`  | PRIVATE | Транзитивно: `pin_mux.h`, clock, SDK headers |
| `sdk_gpio`   | PRIVATE | `fsl_gpio.h` — `GPIO_PinWrite/Read()`        |
